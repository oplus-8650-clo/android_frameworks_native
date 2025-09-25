# Input Device Configuration

There are a number of properties that can be specified for an input device.

[//]: # (best viewed in source with wordwrap off)
| Property                                           | Type                 | Value                                                                                                                                                                       |
| -------------------------------------------------- | -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `audio.mic`                                        | boolean (`0` or `1`) | A boolean represented numerically that indicates whether the device has a microphone.                                                                                       |
| `device.additionalSysfsLedsNode`                   | string               | A string representing the path to search for device lights to be used in addition to searching the device node itself for lights.                                           |
| `device.internal`                                  | boolean (`0` or `1`) | A boolean represented numerically that indicates if this input device is part of the device as opposed to be externally attached.                                           |
| `device.type`                                      | string               | A string representing if the device is of a certain type. Valid values are:<br>-`rotaryEncoder`<br>-`externalStylus`                                                        |
| `device.viewBehavior_smoothScroll`                 | boolean (`0` or `1`) | A boolean that indicates if scrolling from the device should be smooth.                                                                                                     |
| `device.viewBehavior_primaryDirectionalMotionAxis` | string               | A string representing the primary directional motion axis for the device, parsed using `MotionEvent::getAxisFromLabel`. Example valid values include "X", "Y", and "HAT_X". |
