# Reader plugin for Howdy backed by libcamera.
#
# Howdy's other readers open a /dev/video node. On the Intel IPU7 that does not
# work for this machine's infrared camera: OpenCV cannot read the raw capture
# node at all, and libcamera's V4L2 shim cannot select which camera a node maps
# to, because all three cameras claim all 32 nodes. See NOTES.md.
#
# device_path in the config is used as a substring match against libcamera
# camera ids rather than as a filesystem path.

import configparser
import mmap
import os
import subprocess
import sys
import time

import cv2
import numpy as np

import libcamera as lc

V4L2_CTL = "/usr/bin/v4l2-ctl"

# Formats this reader can turn into a frame, in order of preference.
# XRGB8888 is what libcamera's software ISP makes from a Bayer sensor. A
# driver that reports the infrared sensor as what it is, Y8 or Y10, gets no
# ISP in libcamera 0.7, which debayers Bayer input only: the camera then
# offers just its raw R8, R10 and R10_CSI2P. R8 is all Howdy needs.
READABLE = ("XRGB8888", "R8", "R10")


class libcamera_reader:
	"""Looks like the parts of cv2.VideoCapture that Howdy uses."""

	def __init__(self, device_name, device_format=None):
		self.camera_hint = os.path.basename(device_name.rstrip("/"))

		# Sensor settings come from Howdy's own config so they survive being
		# run from PAM, where the environment is not ours. The environment
		# still wins if set, which makes tuning by hand easy.
		cfg = configparser.ConfigParser()
		cfg.read("/etc/howdy/config.ini")

		def opt(env, section, key, default):
			val = os.environ.get(env)
			if val:
				return val
			try:
				v = cfg.get(section, key)
				if v and v != "-1":
					return v
			except Exception:
				pass
			return default

		self.subdev = opt("HOWDY_IR_SUBDEV", "video", "ir_subdev", "") or self._find_subdev()
		self.exposure = opt("HOWDY_IR_EXPOSURE", "video", "exposure", "400")
		self.gain = opt("HOWDY_IR_GAIN", "video", "ir_gain", "24")

		self.cm = lc.CameraManager.singleton()
		match = [c for c in self.cm.cameras if self.camera_hint in c.id]
		if not match:
			raise RuntimeError(
				"no libcamera camera matching %r; available: %s"
				% (self.camera_hint, [c.id for c in self.cm.cameras])
			)
		self.camera = match[0]
		self.camera.acquire()

		# Ask for a format this reader can read, and check that it was kept:
		# validate() swaps a format the camera does not offer for one it
		# does, without a word, and reading that as XRGB8888 gives Howdy a
		# frame a quarter of the true width.
		cfg = self.camera.generate_configuration([lc.StreamRole.Viewfinder])
		offered = [str(f) for f in cfg.at(0).formats.pixel_formats]
		self.format = next((f for f in READABLE if f in offered), None)
		if self.format is None:
			raise RuntimeError("no format this reader can read; the camera offers %s"
					   % offered)
		cfg.at(0).pixel_format = lc.PixelFormat(self.format)
		if self.format != "XRGB8888":
			# A raw format comes in the sensor's own sizes, and the default
			# is the smallest, a binned 320x240. Take the native frame:
			# Howdy scales to its max_height itself.
			cfg.at(0).size = cfg.at(0).formats.range(lc.PixelFormat(self.format)).max
		if (cfg.validate() == lc.CameraConfiguration.Status.Invalid
				or str(cfg.at(0).pixel_format) != self.format):
			raise RuntimeError("libcamera did not keep %s, it gave %s"
					   % (self.format, cfg.at(0).pixel_format))
		self.scfg = cfg.at(0)
		self.camera.configure(cfg)
		self.stream = self.scfg.stream
		self.width = self.scfg.size.width
		self.height = self.scfg.size.height
		self.stride = self.scfg.stride

		self.allocator = lc.FrameBufferAllocator(self.camera)
		self.allocator.allocate(self.stream)
		self.buffers = self.allocator.buffers(self.stream)
		self.maps = {}
		self.requests = []
		for i, buf in enumerate(self.buffers):
			plane = buf.planes[0]
			self.maps[i] = mmap.mmap(
				plane.fd, plane.offset + plane.length,
				mmap.MAP_SHARED, mmap.PROT_READ
			)
			req = self.camera.create_request(i)
			req.add_buffer(self.stream, buf)
			self.requests.append(req)

		self._sensor("exposure=%s" % self.exposure, "analogue_gain=%s" % self.gain,
			     "auto_exposure=1", "led_mode=1")

		self.camera.start()
		for req in self.requests:
			self.camera.queue_request(req)
		self.started = True

	@staticmethod
	def _find_subdev(name="vd55g0"):
		"""Locate the sensor's v4l2 subdev by name.

		The numbering is not stable: it moves when the set of cameras or
		their properties changes, and a hardcoded path then silently
		addresses a different sensor. Ask the kernel instead.
		"""
		import glob
		for path in sorted(glob.glob("/sys/class/video4linux/v4l-subdev*")):
			try:
				with open(os.path.join(path, "name")) as fh:
					if fh.read().strip().startswith(name):
						return "/dev/" + os.path.basename(path)
			except OSError:
				continue
		return ""

	def _sensor(self, *ctrls):
		"""Set sensor controls. Best effort: a failure here is not fatal."""
		if not self.subdev or not os.path.exists(V4L2_CTL):
			return
		try:
			subprocess.run(
				[V4L2_CTL, "-d", self.subdev, "--set-ctrl", ",".join(ctrls)],
				stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
				timeout=5, check=False
			)
		except Exception:
			pass

	def set(self, prop, setting):
		"""Accepted and ignored: libcamera decides format and size."""
		return True

	def get(self, prop):
		"""Answer the frame geometry honestly.

		compare.py computes its scaling factor as max_height / get(HEIGHT),
		so a reader that returns 0 here makes the factor enormous and the
		first resize asks for a hundred gigabytes.
		"""
		if prop == cv2.CAP_PROP_FRAME_WIDTH:
			return float(self.width)
		if prop == cv2.CAP_PROP_FRAME_HEIGHT:
			return float(self.height)
		if prop == cv2.CAP_PROP_FPS:
			return 0.0
		return 0.0

	def probe(self):
		return True

	def record(self):
		return True

	def grab(self):
		ret, _frame = self.read()
		return ret

	def read(self):
		"""Return (ok, frame) with frame as BGR, as Howdy expects."""
		deadline = time.monotonic() + 2.0
		while time.monotonic() < deadline:
			ready = self.cm.get_ready_requests()
			if not ready:
				time.sleep(0.005)
				continue
			frame = None
			for req in ready:
				i = req.cookie
				raw = np.frombuffer(
					self.maps[i], dtype=np.uint8,
					count=self.stride * self.height
				)
				if self.format == "XRGB8888":
					# the sensor is monochrome, so any one of the
					# colour bytes carries the image
					mono = raw.reshape(self.height, self.stride // 4, 4)[:, :self.width, 0]
				elif self.format == "R8":
					mono = raw.reshape(self.height, self.stride)[:, :self.width]
				else:
					# R10: ten bits in sixteen, little-endian; the top eight
					mono = (raw.view("<u2").reshape(self.height, self.stride // 2)
						[:, :self.width] >> 2).astype(np.uint8)
				frame = np.dstack([mono, mono, mono])
				req.reuse()
				self.camera.queue_request(req)
			if frame is not None:
				return True, frame
		return False, None

	def release(self):
		"""Tear down in the order libcamera requires, exactly once.

		stop() before anything is freed, then the requests, then the
		mappings, then the allocator, then the camera. Calling
		Camera::release() twice aborts inside PipelineHandler::stop(),
		which is why this is guarded rather than merely idempotent-looking.
		"""
		if getattr(self, "_released", False):
			return
		self._released = True

		if getattr(self, "started", False):
			try:
				self.camera.stop()
			except Exception:
				pass
			self.started = False

		self._sensor("led_mode=0")

		self.requests = []
		for m in getattr(self, "maps", {}).values():
			try:
				m.close()
			except Exception:
				pass
		self.maps = {}
		self.buffers = []
		self.allocator = None

		try:
			self.camera.release()
		except Exception:
			pass

	def __del__(self):
		try:
			self.release()
		except Exception:
			pass
