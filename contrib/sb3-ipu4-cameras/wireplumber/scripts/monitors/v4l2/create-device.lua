-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-v4l2")

config = {}
config.rules = Conf.get_section_as_json ("monitor.v4l2.rules", Json.Array {})

function createV4l2camNode (parent, id, type, factory, properties)
  mutils:register_cam_node (parent, id, factory, properties)
end

AsyncEventHook {
  name = "monitor/v4l2/create-device",
  after = "monitor/v4l2/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-v4l2-device" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local properties = event:get_data ("device-properties")
        local factory = event:get_data ("factory")
        local parent = event:get_subject ()
        local id = event:get_data ("device-sub-id")

        -- apply properties from rules defined in JSON .conf file
        properties = JsonUtils.match_rules_update_properties (config.rules, properties)

        if cutils.parseBool (properties ["device.disabled"]) then
          log:notice ("V4L2 device " .. properties["device.name"] .. " disabled")
          -- SP7 fix (wireplumber 0.5.17): this is an AsyncEventHook, so returning
          -- without advancing the transition stalls the dispatcher and no V4L2
          -- device enumerated after the first disabled one is ever created.
          transition:advance ()
          return
        end

        -- create the device
        local device = SpaDevice (factory, properties)
        if device == nil then
          transition:return_error ("Failed to create '" .. factory .. "' device")
          return
        end

        -- handle signals
        device:connect ("create-object", createV4l2camNode)

        -- activate the device
        device:activate (Features.ALL, function (d, e)
          if e ~= nil then
            transition:return_error ("Failed to activate V4L2 device " ..
                tostring (properties ["device.name"]) .. ": " .. e)
            return
          end

          parent:store_managed_object (id, device)
          transition:advance ()
        end)
      end
    }
  }
}:register ()
