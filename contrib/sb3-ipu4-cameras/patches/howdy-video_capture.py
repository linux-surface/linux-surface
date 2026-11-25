# Top level class for a video capture providing simplified API's for common
# functions

# Import required modules
import configparser
import cv2
import os
import sys
import time

from i18n import _

# Class to provide boilerplate code to build a video recorder with the
# correct settings from the config file.
#
# The internal recorder can be accessed with 'video_capture.internal'


class VideoCapture:
	def __init__(self, config):
		"""
		Creates a new VideoCapture instance depending on the settings in the
		provided config file.

		Config can either be a string to the path, or a pre-setup configparser.
		"""

		# Parse config from string if needed
		if isinstance(config, str):
			self.config = configparser.ConfigParser()
			self.config.read(config)
		else:
			self.config = config

		# Check device path
		if not os.path.exists(self.config.get("video", "device_path")):
			if self.config.getboolean("video", "warn_no_device", fallback=True):
				print(_("Howdy could not find a camera device at the path specified in the config file."))
				print(_("It is very likely that the path is not configured correctly, please edit the 'device_path' config value by running:"))
				print("\n\tsudo howdy config\n")
			sys.exit(14)

		# Create reader
		# The internal video recorder
		self.internal = None
		# The frame width
		self.fw = None
		# The frame height
		self.fh = None
		self._create_reader()

		# Request a frame to wake the camera up
		self.internal.grab()

	def __del__(self):
		"""
		Frees resources when destroyed
		"""
		if self is not None:
			try:
				self.internal.release()
			except AttributeError as err:
				pass

	def release(self):
		"""
		Release cameras
		"""
		if self is not None:
			self.internal.release()

	def read_frame(self):
		"""
		Reads a frame, returns the frame and an attempted grayscale conversion of
		the frame in a tuple:

		(frame, grayscale_frame)

		If the grayscale conversion fails, both items in the tuple are identical.
		"""

		# The IR loopback has no frame until webcamd finishes STREAMON.
		# One failed read used to abort as "cannot open camera" (PAM exit 14)
		# before the sensor was up. Retry inside the configured timeout.
		wait_s = 2.0
		deadline = time.time() + wait_s
		frame = None
		while time.time() < deadline:
			ret, frame = self.internal.read()
			if ret and frame is not None and getattr(frame, "size", 0):
				break
			time.sleep(0.05)
		else:
			print(_("Failed to read camera specified in the 'device_path' config option, aborting"))
			sys.exit(14)

		try:
			# Convert from color to grayscale
			# First processing of frame, so frame errors show up here
			gsframe = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
		except RuntimeError:
			gsframe = frame
		except cv2.error:
			print("\nAn error occurred in OpenCV\n")
			raise
		return frame, gsframe

	def _create_reader(self):
		"""
		Sets up the video reader instance
		"""
		recording_plugin = self.config.get("video", "recording_plugin", fallback="opencv")

		if recording_plugin == "ffmpeg":
			# Set the capture source for ffmpeg
			from recorders.ffmpeg_reader import ffmpeg_reader
			self.internal = ffmpeg_reader(
				self.config.get("video", "device_path"),
				self.config.get("video", "device_format", fallback="v4l2")
			)

		elif recording_plugin == "pyv4l2":
			# Set the capture source for pyv4l2
			from recorders.pyv4l2_reader import pyv4l2_reader
			self.internal = pyv4l2_reader(
				self.config.get("video", "device_path"),
				self.config.get("video", "device_format", fallback="v4l2")
			)

		else:
			# Start video capture on the IR camera through OpenCV
			api = getattr(cv2, "CAP_V4L2", cv2.CAP_V4L)
			self.internal = cv2.VideoCapture(
				self.config.get("video", "device_path"),
				api,
			)
			# Set the capture frame rate
			# Without this the first detected (and possibly lower) frame rate is used, -1 seems to select the highest
			# Use 0 as a fallback to avoid breaking an existing setup, new installs should default to -1
			self.fps = self.config.getint("video", "device_fps", fallback=0)
			if self.fps != 0:
				self.internal.set(cv2.CAP_PROP_FPS, self.fps)

		# Force MJPEG decoding if true
		if self.config.getboolean("video", "force_mjpeg", fallback=False):
			# Set a magic number, will enable MJPEG but is badly documentated
			self.internal.set(cv2.CAP_PROP_FOURCC, 1196444237)

		# Set the frame width and height if requested
		self.fw = self.config.getint("video", "frame_width", fallback=-1)
		self.fh = self.config.getint("video", "frame_height", fallback=-1)
		if self.fw != -1:
			self.internal.set(cv2.CAP_PROP_FRAME_WIDTH, self.fw)
		if self.fh != -1:
			self.internal.set(cv2.CAP_PROP_FRAME_HEIGHT, self.fh)
