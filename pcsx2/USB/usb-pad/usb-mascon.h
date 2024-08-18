// SPDX-FileCopyrightText: 2002-2024 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "USB/deviceproxy.h"
#include "USB/qemu-usb/qusb.h"
#include "USB/qemu-usb/desc.h"

namespace usb_pad
{
	enum MasconTypes
	{
		MT_TYPE2, // TCPP20009 or similar
		MT_SHINKANSEN, // TCPP20011
		MT_RYOJOUHEN, // TCPP20014
		MT_COUNT,
	};

	class MasconDevice final : public DeviceProxy
	{
	public:
		USBDevice* CreateDevice(SettingsInterface& si, u32 port, u32 subtype) const override;
		const char* Name() const override;
		const char* TypeName() const override;
		std::span<const char*> SubTypes() const override;
		void UpdateSettings(USBDevice* dev, SettingsInterface& si) const override;
		std::span<const SettingInfo> Settings(u32 subtype) const override;
		float GetBindingValue(const USBDevice* dev, u32 bind_index) const override;
		void SetBindingValue(USBDevice* dev, u32 bind_index, float value) const override;
		std::span<const InputBindingInfo> Bindings(u32 subtype) const override;
		bool Freeze(USBDevice* dev, StateWrapper& sw) const override;
	};

	struct MasconState
	{
		MasconState(u32 port_, MasconTypes type_);
		~MasconState();

		void UpdateSettings(SettingsInterface& si, const char* devname);

		float GetBindValue(u32 bind) const;
		void SetBindValue(u32 bind, float value);

		void Reset();
		int TokenIn(u8* buf, int len);

		void UpdateHatSwitch() noexcept;

		USBDevice dev{};
		USBDesc desc{};
		USBDescDevice desc_dev{};

		u32 port = 0;
		MasconTypes type = MT_TYPE2;
		bool passthrough = false;

		struct
		{
			// intermediate state, resolved at query time
			bool hat_left : 1;
			bool hat_right : 1;
			bool hat_up : 1;
			bool hat_down : 1;

			u8 power; // 255 is fully applied
			u8 brake; // 255 is fully applied
			u8 hatswitch; // direction
			u8 buttons; // active high
		} data = {};
	};

	// Taito Densha Controllers as described at:
	// https://marcriera.github.io/ddgo-controller-docs/controllers/usb/
#define DEFINE_DCT_DEV_DESCRIPTOR(prefix, subclass, product) \
	static const uint8_t prefix##_dev_descriptor[] = { \
		/* bLength             */ USB_DEVICE_DESC_SIZE, \
		/* bDescriptorType     */ USB_DEVICE_DESCRIPTOR_TYPE, \
		/* bcdUSB              */ WBVAL(0x0110), /* USB 1.1 */ \
		/* bDeviceClass        */ 0xFF, \
		/* bDeviceSubClass     */ subclass, \
		/* bDeviceProtocol     */ 0x00, \
		/* bMaxPacketSize0     */ 0x08, \
		/* idVendor            */ WBVAL(0x0ae4), \
		/* idProduct           */ WBVAL(product), \
		/* bcdDevice           */ WBVAL(0x0102), /* 1.02 */ \
		/* iManufacturer       */ 0x01, \
		/* iProduct            */ 0x02, \
		/* iSerialNumber       */ 0x03, \
		/* bNumConfigurations  */ 0x01, \
	}

	// These settings are common across multiple models.
	static const uint8_t taito_denshacon_config_descriptor[] = {
		USB_CONFIGURATION_DESC_SIZE, // bLength
		USB_CONFIGURATION_DESCRIPTOR_TYPE, // bDescriptorType
		WBVAL(25), // wTotalLength
		0x01, // bNumInterfaces
		0x01, // bConfigurationValue
		0x00, // iConfiguration (String Index)
		0xA0, // bmAttributes
		0xFA, // bMaxPower 500mA

		USB_INTERFACE_DESC_SIZE, // bLength
		USB_INTERFACE_DESCRIPTOR_TYPE, // bDescriptorType
		0x00, // bInterfaceNumber
		0x00, // bAlternateSetting
		0x01, // bNumEndpoints
		USB_CLASS_HID, // bInterfaceClass
		0x00, // bInterfaceSubClass
		0x00, // bInterfaceProtocol
		0x00, // iInterface (String Index)

		USB_ENDPOINT_DESC_SIZE, // bLength
		USB_ENDPOINT_DESCRIPTOR_TYPE, // bDescriptorType
		USB_ENDPOINT_IN(1), // bEndpointAddress (IN/D2H)
		USB_ENDPOINT_TYPE_INTERRUPT, // bmAttributes (Interrupt)
		WBVAL(8), // wMaxPacketSize
		0x14, // bInterval 20 (unit depends on device speed)
		// 25 bytes (43 total with dev descriptor)
	};

#define MASCON_PREAMBLE \
	0x05, 0x01, \
		0x09, 0x04, \
		0xA1, 0x01

#define MASCON_PAD_BYTE \
	0x75, 0x08, \
		0x95, 0x01, \
		0x81, 0x01

#define MASCON_AXES \
	0x09, 0x01, \
		0xA1, 0x00, \
		0x09, 0x30, \
		0x09, 0x31, \
		0x09, 0x32, \
		0x15, 0x00, \
		0x26, 0xFF, 0x00, \
		0x75, 0x08, \
		0x95, 0x03, \
		0x81, 0x02, \
		0xC0

#define MASCON_DPAD \
	0x05, 0x01, \
		0x25, 0x07, \
		0x46, 0x3B, 0x01, \
		0x75, 0x04, \
		0x95, 0x01, \
		0x65, 0x14, \
		0x09, 0x39, \
		0x81, 0x42, \
		0x65, 0x00, \
		0x95, 0x01

#define MASCON_BUTTONS(max) \
	0x81, 0x01, \
		0x05, 0x09, \
		0x19, 0x01, \
		0x29, max, \
		0x15, 0x00, \
		0x25, 0x01, \
		0x35, 0x00, \
		0x45, 0x01, \
		0x75, 0x01, \
		0x95, max, \
		0x81, 0x02, \
		0x95, 8 - max, \
		0x81, 0x01

#define MASCON_END 0xC0 // End Collection

	// ---- Two handle controller "Type 2" ----

	static const USBDescStrings dct01_desc_strings = {
		"",
		"TAITO",
		"TAITO_DENSYA_CON_T01",
		"TCPP20009",
	};

	DEFINE_DCT_DEV_DESCRIPTOR(dct01, 0x04, 0x0004);

	// https://marcriera.github.io/ddgo-controller-docs/controllers/usb/descriptors/tcpp20009_hid-report-descriptor.txt
	static const uint8_t dct01_hid_report_descriptor[] = {
		MASCON_PREAMBLE,
		MASCON_PAD_BYTE,
		MASCON_AXES, // Brake, Power, Horn
		MASCON_DPAD,
		MASCON_BUTTONS(6),
		MASCON_END,
	};

	// ---- Shinkansen controller ----

	static const USBDescStrings dct02_desc_strings = {
		"",
		"TAITO",
		"TAITO_DENSYA_CON_T02",
		"TCPP20011",
	};

	DEFINE_DCT_DEV_DESCRIPTOR(dct02, 0x05, 0x0005);

	// https://marcriera.github.io/ddgo-controller-docs/controllers/usb/descriptors/tcpp20011_hid-report-descriptor.txt
	static const uint8_t dct02_hid_report_descriptor[] = {
		MASCON_PREAMBLE,
		MASCON_AXES, // Brake, Power, Pedal
		MASCON_DPAD,
		MASCON_BUTTONS(6),
		MASCON_PAD_BYTE,
		MASCON_END,
	};

	// ---- Ryojouhen controller ----

	static const USBDescStrings dct03_desc_strings = {
		"",
		"TAITO",
		"TAITO_DENSYA_CON_T03",
		"TCPP20014",
	};

	DEFINE_DCT_DEV_DESCRIPTOR(dct03, 0xFF, 0x0007);

	// https://marcriera.github.io/ddgo-controller-docs/controllers/usb/descriptors/tcpp20014_hid-report-descriptor.txt
	static const uint8_t dct03_hid_report_descriptor[] = {
		MASCON_PREAMBLE,
		MASCON_AXES, // Brake, Power, Pedal
		MASCON_DPAD,
		MASCON_BUTTONS(7),
		MASCON_PAD_BYTE,
		MASCON_PAD_BYTE,
		MASCON_PAD_BYTE,
		MASCON_END,
	};

} // namespace usb_pad
