-- Surface Book 3: spa v4l2-udev MAX_DEVICES=64 fills with IPU ISYS Capture
-- before virtual loopbacks. Manually register Surface-Back-* cameras.
cutils = require ("common-utils")
mutils = require ("monitor-utils")
log = Log.open_topic ("s-monitors-v4l2-surface")

config = {}
config.rules = Conf.get_section_as_json ("monitor.v4l2.rules", Json.Array {})

surface_devs = {}

local function create_surface_cam (path, card, priority)
  local properties = {
    ["device.name"] = "v4l2_device." .. card:gsub ("([^%w_%-%.])", "_"),
    ["device.description"] = card,
    ["device.product.name"] = card,
    ["device.nick"] = card,
    ["device.api"] = "v4l2",
    ["api.v4l2.path"] = path,
    ["media.class"] = "Video/Device",
    ["api.v4l2.cap.card"] = card,
    ["api.v4l2.cap.driver"] = "v4l2loopback",
    ["priority.session"] = priority,
    ["node.pause-on-idle"] = true,
    ["session.suspend-timeout-seconds"] = 5,
  }

  properties = JsonUtils.match_rules_update_properties (config.rules, properties)
  if cutils.parseBool (properties ["device.disabled"]) then
    log:notice ("Surface device disabled by rules: " .. card)
    return
  end

  local device = SpaDevice ("api.v4l2.device", properties)
  if device == nil then
    log:warning ("Failed to create SpaDevice for " .. path)
    return
  end

  device:connect ("create-object", function (parent, id, type, factory, props)
    mutils:register_cam_node (parent, id, factory, props)
  end)

  device:activate (Features.ALL, function (d, e)
    if e ~= nil then
      log:warning ("Activate failed " .. path .. ": " .. tostring (e))
      return
    end
    log:notice ("Surface camera ready: " .. card .. " @ " .. path)
  end)

  table.insert (surface_devs, device)
end

create_surface_cam ("/dev/video58", "Surface-Back-Standard", 2300)
create_surface_cam ("/dev/video59", "Surface-Back-HQ", 2250)
create_surface_cam ("/dev/video60", "Surface-Back-Fast", 2200)
