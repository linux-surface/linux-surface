#!/bin/bash

# Use uhubctl to power down and power on only the Surface Touch Device
uhubctl --vendor 045e:0923 --ports 3 --action off
sleep 0.2
uhubctl --vendor 045e:0923 --ports 3 --action on

