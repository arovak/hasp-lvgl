/* MIT License - Copyright (c) 2019-2021 Francis Van Roie
   For full license information read the LICENSE file in the project folder */

#ifndef HASP_DEVICE_WINDOWS_H
#define HASP_DEVICE_WINDOWS_H

#include <cstdint>
#include <string>
#include "Windows.h"

#include "hasp_conf.h"
#include "../device.h"

#if defined(WINDOWS)

namespace dev {

class Win32Device : public BaseDevice {

  public:
    Win32Device()
    {
        char buffer[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD length = sizeof(buffer);

        if(GetComputerNameExA((COMPUTER_NAME_FORMAT)ComputerNameNetBIOS, buffer, &length)) {
            _hostname = buffer;
        } else if(GetComputerNameExA((COMPUTER_NAME_FORMAT)ComputerNameDnsHostname, buffer, &length)) {
            _hostname = buffer;
        } else if(GetComputerNameExA((COMPUTER_NAME_FORMAT)ComputerNamePhysicalDnsHostname, buffer, &length)) {
            _hostname = buffer;
        } else if(GetComputerNameExA((COMPUTER_NAME_FORMAT)ComputerNamePhysicalDnsDomain, buffer, &length)) {
            _hostname = buffer;
        } else {
            _hostname = "localhost";
        }

        // _backlight_pin   = -1;
        _backlight_power = 1;
        _backlight_level = 100;
    }

    void reboot() override;
    void show_info() override;

    const char* get_hostname();
    void set_hostname(const char*);
    const char* get_core_version();
    const char* get_display_driver();

    void set_backlight_pin(uint8_t pin);
    void set_backlight_level(uint8_t val);
    uint8_t get_backlight_level();
    void set_backlight_power(bool power);
    bool get_backlight_power();

    size_t get_free_max_block();
    size_t get_free_heap();
    uint8_t get_heap_fragmentation();
    uint16_t get_cpu_frequency();

    bool is_system_pin(uint8_t pin) override;

  private:
    std::string _hostname;

    uint8_t _backlight_pin;
    uint8_t _backlight_level;
    uint8_t _backlight_power;

    void update_backlight();
};

} // namespace dev

using dev::Win32Device;
extern dev::Win32Device haspDevice;

#endif // WINDOWS

#endif // HASP_DEVICE_WINDOWS_H