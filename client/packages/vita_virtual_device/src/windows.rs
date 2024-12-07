use std::time::Instant;
use std::{ffi::OsString, time::Duration};

use vigem_client::{
    BatteryStatus, Client, DS4Buttons, DS4ReportExBuilder, DS4SpecialButtons, DS4Status,
    DS4TouchPoint, DS4TouchReport, DpadDirection as VigemDpadDirection, DualShock4Wired, TargetId,
};

use windows::Win32::UI::Input::KeyboardAndMouse::{
    SendInput, INPUT, INPUT_0, INPUT_KEYBOARD, KEYBDINPUT, KEYBD_EVENT_FLAGS, KEYEVENTF_KEYUP,
    VIRTUAL_KEY, VK_VOLUME_DOWN, VK_VOLUME_UP,
};

use crate::virtual_button::{Button, DpadDirection};
use crate::virtual_config::{Config, ConfigBuilder, TouchConfig, TriggerConfig};
use crate::virtual_touch::{Point, TouchAction};
use crate::virtual_utils::{compute_dpad_direction, get_pressed_buttons};
use crate::{f32_to_i16, VitaVirtualDevice, FRONT_TOUCHPAD_RECT, REAR_TOUCHPAD_RECT};

unsafe fn simulate_key_press(vk: VIRTUAL_KEY) -> windows::core::Result<()> {
    let inputs = &mut [
        INPUT {
            r#type: INPUT_KEYBOARD,
            Anonymous: INPUT_0 {
                ki: KEYBDINPUT {
                    wVk: vk,
                    wScan: 0,
                    dwFlags: KEYBD_EVENT_FLAGS(0),
                    time: 0,
                    dwExtraInfo: 0,
                },
            },
        },
        INPUT {
            r#type: INPUT_KEYBOARD,
            Anonymous: INPUT_0 {
                ki: KEYBDINPUT {
                    wVk: vk,
                    wScan: 0,
                    dwFlags: KEYEVENTF_KEYUP,
                    time: 0,
                    dwExtraInfo: 0,
                },
            },
        },
    ];

    let result = SendInput(inputs, std::mem::size_of::<INPUT>() as i32);
    if result == 0 {
        Err(windows::core::Error::from_win32())
    } else {
        Ok(())
    }
}

fn change_volume_by_key(delta: f32) -> windows::core::Result<()> {
    unsafe {
        if delta > 0.0 {
            simulate_key_press(VK_VOLUME_UP)?;
        } else if delta < 0.0 {
            simulate_key_press(VK_VOLUME_DOWN)?;
        }
        Ok(())
    }
}

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("Failed to connect to the client")]
    ConnectionFailed(#[source] vigem_client::Error),
    #[error("Failed to plugin the target")]
    PluginTargetFailed(#[source] vigem_client::Error),
    #[error("Sending report failed")]
    SendReportFailed(#[source] vigem_client::Error),
    #[error("Invalid configuration: {0}")]
    InvalidConfig(String),
}

fn map_button_to_ds4(button: Button) -> u16 {
    match button {
        Button::ThumbRight => DS4Buttons::THUMB_RIGHT,
        Button::ThumbLeft => DS4Buttons::THUMB_LEFT,
        Button::Options => DS4Buttons::OPTIONS,
        Button::Share => DS4Buttons::SHARE,
        Button::TriggerRight => DS4Buttons::TRIGGER_RIGHT,
        Button::TriggerLeft => DS4Buttons::TRIGGER_LEFT,
        Button::ShoulderRight => DS4Buttons::SHOULDER_RIGHT,
        Button::ShoulderLeft => DS4Buttons::SHOULDER_LEFT,
        Button::Triangle => DS4Buttons::TRIANGLE,
        Button::Circle => DS4Buttons::CIRCLE,
        Button::Cross => DS4Buttons::CROSS,
        Button::Square => DS4Buttons::SQUARE,
    }
}

fn map_dpad_direction_to_ds4(dpad: DpadDirection) -> VigemDpadDirection {
    match dpad {
        DpadDirection::North => VigemDpadDirection::North,
        DpadDirection::NorthEast => VigemDpadDirection::NorthEast,
        DpadDirection::East => VigemDpadDirection::East,
        DpadDirection::SouthEast => VigemDpadDirection::SouthEast,
        DpadDirection::South => VigemDpadDirection::South,
        DpadDirection::SouthWest => VigemDpadDirection::SouthWest,
        DpadDirection::West => VigemDpadDirection::West,
        DpadDirection::NorthWest => VigemDpadDirection::NorthWest,
        DpadDirection::None => VigemDpadDirection::None,
    }
}

fn process_touch_reports(
    touch_reports: &[vita_reports::TouchReport],
    touch_config: &Option<TouchConfig>,
    buttons: &mut DS4Buttons,
) {
    if let Some(TouchConfig::Zones(zones)) = touch_config {
        for touch in touch_reports {
            if let Some(zone) = zones.locate_at_point(&Point(touch.x.into(), touch.y.into())) {
                if let Some(action) = zone.action {
                    match action {
                        TouchAction::Button(button) => {
                            let ds4_button = map_button_to_ds4(button);
                            *buttons |= ds4_button;
                        }
                        TouchAction::Dpad(direction) => {
                            let ds4_dpad = map_dpad_direction_to_ds4(direction);
                            *buttons = buttons.dpad(ds4_dpad);
                        }
                    }
                }
            }
        }
    }
}

pub struct VitaDevice {
    ds4_target: DualShock4Wired<Client>,
    config: Config,
    touch_state: bool,
    touch_start_time: Option<Instant>,
}

impl VitaDevice {
    pub fn create(config_name: &str) -> crate::Result<Self> {
        let client = Client::connect().map_err(Error::ConnectionFailed)?;
        let mut ds4_target = DualShock4Wired::new(client, TargetId::DUALSHOCK4_WIRED);

        ds4_target.plugin().map_err(Error::PluginTargetFailed)?;
        ds4_target.wait_ready().map_err(Error::PluginTargetFailed)?;
        // Wait for the device to be ready, because the ioctl doesn't seem to work
        std::thread::sleep(Duration::from_millis(100));

        // Select the configuration depending on the name
        let config = match config_name {
            "standart" => Config::rear_rl2_front_rl3(),
            "alt_triggers" => Config::rear_rl1_front_rl3_vitatriggers_rl2(),
            "rear_touchpad" => Config::front_top_rl2_bottom_rl3_rear_touchpad(),
            "front_touchpad" => Config::rear_top_rl2_bottom_rl3_front_touchpad(),
            _ => {
                return Err(crate::Error::Windows(Error::InvalidConfig(
                    config_name.to_string(),
                )))
            }
        };

        Ok(VitaDevice {
            ds4_target,
            config: config,
            touch_state: false,
            touch_start_time: None,
        })
    }

    fn create_touchpad_report(&self, report: &vita_reports::MainReport) -> Option<DS4TouchReport> {
        if let Some(TouchConfig::Touchpad) = self.config.front_touch_config {
            let points = report
                .front_touch
                .reports
                .iter()
                .map(|touch| {
                    DS4TouchPoint::new(
                        touch.x as u16,
                        (touch.y as f32 * (942.0 / FRONT_TOUCHPAD_RECT.1.y() as f32)) as u16,
                    )
                })
                .collect::<Vec<_>>();

            Some(DS4TouchReport::new(
                0,
                points.get(0).cloned(),
                points.get(1).cloned(),
            ))
        } else if let Some(TouchConfig::Touchpad) = self.config.rear_touch_config {
            let points = report
                .back_touch
                .reports
                .iter()
                .map(|touch| {
                    DS4TouchPoint::new(
                        touch.x as u16,
                        (touch.y as f32 * (942.0 / REAR_TOUCHPAD_RECT.1.y() as f32)) as u16,
                    )
                })
                .collect::<Vec<_>>();

            Some(DS4TouchReport::new(
                0,
                points.get(0).cloned(),
                points.get(1).cloned(),
            ))
        } else {
            None
        }
    }
}

impl VitaVirtualDevice<&ConfigBuilder> for VitaDevice {
    type Config = Config;

    fn identifiers(&self) -> Option<&[OsString]> {
        None
    }

    #[inline]
    fn get_config(&self) -> &Self::Config {
        &self.config
    }

    #[inline]
    fn set_config(&mut self, config: &ConfigBuilder) -> crate::Result<()> {
        if let Some(front_touch_config) = &config.front_touch_config {
            self.config.front_touch_config = front_touch_config.clone();
        }

        if let Some(rear_touch_config) = &config.rear_touch_config {
            self.config.rear_touch_config = rear_touch_config.clone();
        }

        if let Some(trigger_config) = config.trigger_config {
            self.config.trigger_config = trigger_config;
        }

        Ok(())
    }

    fn send_report(&mut self, report: vita_reports::MainReport) -> crate::Result<()> {
        // Calculate the direction of the D-Pad
        let dpad_direction = compute_dpad_direction(&report.buttons);
        let ds4_dpad = map_dpad_direction_to_ds4(dpad_direction);

        // Get the pressed buttons
        let pressed_buttons = get_pressed_buttons(&report.buttons, self.config.trigger_config);
        if report.buttons.vol_up {
            change_volume_by_key(0.02).expect("Failed to increase volume");
        }
        if report.buttons.vol_down {
            change_volume_by_key(-0.02).expect("Failed to increase volume");
        }

        // Create DS4Buttons object
        let mut buttons = DS4Buttons::new().dpad(ds4_dpad);

        for button in pressed_buttons {
            let ds4_button = map_button_to_ds4(button);
            buttons |= ds4_button;
        }

        // Process touch reports
        process_touch_reports(
            &report.front_touch.reports,
            &self.config.front_touch_config,
            &mut buttons,
        );

        process_touch_reports(
            &report.back_touch.reports,
            &self.config.rear_touch_config,
            &mut buttons,
        );

        // Trigger processing for Trigger configuration
        let (mut pwr_trigger_l, mut pwr_trigger_r) = (0u8, 0u8);
        if let TriggerConfig::Trigger = self.config.trigger_config {
            if report.buttons.lt {
                pwr_trigger_l = 255;
            }
            if report.buttons.rt {
                pwr_trigger_r = 255;
            }
        }

        // Handling special touchpad buttons
        let is_touching = match (
            &self.config.front_touch_config,
            &self.config.rear_touch_config,
        ) {
            (Some(TouchConfig::Touchpad), _) => !report.front_touch.reports.is_empty(),
            (_, Some(TouchConfig::Touchpad)) => !report.back_touch.reports.is_empty(),
            _ => false,
        };

        let mut special_buttons = DS4SpecialButtons::new();

        if is_touching && !self.touch_state {
            self.touch_start_time = Some(Instant::now());
        } else if !is_touching && self.touch_state {
            if let Some(start_time) = self.touch_start_time {
                let duration = Instant::now().duration_since(start_time);
                if duration < Duration::from_millis(150) {
                    special_buttons = special_buttons.touchpad(true);
                }
            }
            self.touch_start_time = None;
        }

        special_buttons = special_buttons.ps_home(report.buttons.ps);

        self.touch_state = is_touching;

        // Создаем touchpad report
        let touchpad = self.create_touchpad_report(&report);

        // Convert the vita accel range [-4.0, 4.0] to the dualshock 4 range [-32768, 32768]
        let accel_x_i16 = f32_to_i16(-report.motion.accelerometer.x, -4.0, 4.0); //inverted
        let accel_y_i16 = f32_to_i16(report.motion.accelerometer.y, -4.0, 4.0);
        let accel_z_i16 = f32_to_i16(-report.motion.accelerometer.z, -4.0, 4.0); //inverted

        // Convert the vita gyro range [-35.0, 35.0] to the dualshock 4 range [-32768, 32768]
        let gyro_x_i16 = f32_to_i16(report.motion.gyro.x, -35.0, 35.0);
        let gyro_y_i16 = f32_to_i16(-report.motion.gyro.y, -35.0, 35.0); //inverted
        let gyro_z_i16 = f32_to_i16(report.motion.gyro.z, -35.0, 35.0);

        let report = DS4ReportExBuilder::new()
            .thumb_lx(report.lx)
            .thumb_ly(report.ly)
            .thumb_rx(report.rx)
            .thumb_ry(report.ry)
            .buttons(buttons)
            .touch_reports(touchpad, None, None)
            .gyro_x(gyro_x_i16)
            .gyro_y(gyro_z_i16)
            .gyro_z(gyro_y_i16)
            .accel_x(accel_x_i16)
            .accel_y(accel_z_i16)
            .accel_z(accel_y_i16)
            .trigger_l(pwr_trigger_l)
            .trigger_r(pwr_trigger_r)
            .status(DS4Status::with_battery_status(BatteryStatus::Charging(
                (report.charge_percent / 10).min(10),
            )))
            .special(special_buttons)
            .build();

        self.ds4_target
            .update_ex(&report)
            .map_err(Error::SendReportFailed)?;

        Ok(())
    }
}
