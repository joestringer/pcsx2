// SPDX-FileCopyrightText: 2002-2024 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "usb-mascon.h"

#include "common/Console.h"
#include "Host.h"
#include "IconsPromptFont.h"
#include "Input/InputManager.h"
#include "StateWrapper.h"
#include "USB/deviceproxy.h"
#include "USB/USB.h"
#include "USB/qemu-usb/USBinternal.h"
#include "USB/qemu-usb/desc.h"

namespace usb_pad
{
	const char* MasconDevice::Name() const
	{
		return TRANSLATE_NOOP("USB", "Densha Controller");
	}

	const char* MasconDevice::TypeName() const
	{
		return "DenshaCon";
	}

	std::span<const char*> MasconDevice::SubTypes() const
	{
		static const char* subtypes[] = {
			TRANSLATE_NOOP("USB", "Type 2"),
			TRANSLATE_NOOP("USB", "Shinkansen"),
		};
		return subtypes;
	}

	enum MasconControlID
	{
		CID_MC_POWER,
		CID_MC_BRAKE,
		CID_MC_UP,
		CID_MC_RIGHT,
		CID_MC_DOWN,
		CID_MC_LEFT,

		// TCPP20009 sends the buttons in this order in the relevant byte
		CID_MC_B,
		CID_MC_A,
		CID_MC_C,
		CID_MC_D,
		CID_MC_SELECT,
		CID_MC_START,

		LENGTH,
		BUTTONS_OFFSET = CID_MC_B,
	};

	std::span<const InputBindingInfo> MasconDevice::Bindings(u32 subtype) const
	{
		switch (subtype)
		{
			case MT_TYPE2:
			case MT_SHINKANSEN:
			{
				static constexpr const InputBindingInfo bindings[] = {
					{"Power", TRANSLATE_NOOP("Pad", "Power"), ICON_PF_LEFT_ANALOG_DOWN, InputBindingInfo::Type::Axis, CID_MC_POWER, GenericInputBinding::LeftStickDown},
					{"Brake", TRANSLATE_NOOP("Pad", "Brake"), ICON_PF_LEFT_ANALOG_UP, InputBindingInfo::Type::Axis, CID_MC_BRAKE, GenericInputBinding::LeftStickUp},
					{"Up", TRANSLATE_NOOP("Pad", "D-Pad Up"), ICON_PF_DPAD_UP, InputBindingInfo::Type::Button, CID_MC_UP, GenericInputBinding::DPadUp},
					{"Down", TRANSLATE_NOOP("Pad", "D-Pad Down"), ICON_PF_DPAD_DOWN, InputBindingInfo::Type::Button, CID_MC_DOWN, GenericInputBinding::DPadDown},
					{"Left", TRANSLATE_NOOP("Pad", "D-Pad Left"), ICON_PF_DPAD_LEFT, InputBindingInfo::Type::Button, CID_MC_LEFT, GenericInputBinding::DPadLeft},
					{"Right", TRANSLATE_NOOP("Pad", "D-Pad Right"), ICON_PF_DPAD_RIGHT, InputBindingInfo::Type::Button, CID_MC_RIGHT, GenericInputBinding::DPadRight},
					{"A", TRANSLATE_NOOP("Pad", "A Button"), ICON_PF_KEY_A, InputBindingInfo::Type::Button, CID_MC_A, GenericInputBinding::Square},
					{"B", TRANSLATE_NOOP("Pad", "B Button"), ICON_PF_KEY_B, InputBindingInfo::Type::Button, CID_MC_B, GenericInputBinding::Cross},
					{"C", TRANSLATE_NOOP("Pad", "C Button"), ICON_PF_KEY_C, InputBindingInfo::Type::Button, CID_MC_C, GenericInputBinding::Circle},
					{"D", TRANSLATE_NOOP("Pad", "D Button"), ICON_PF_KEY_D, InputBindingInfo::Type::Button, CID_MC_D, GenericInputBinding::Triangle},
					{"Select", TRANSLATE_NOOP("Pad", "Select"), ICON_PF_SELECT_SHARE, InputBindingInfo::Type::Button, CID_MC_SELECT, GenericInputBinding::Select},
					{"Start", TRANSLATE_NOOP("Pad", "Start"), ICON_PF_START, InputBindingInfo::Type::Button, CID_MC_START, GenericInputBinding::Start},
				};

				return bindings;
			}
			default:
				break;
		}
		return {};
	}

	static void mascon_handle_data(USBDevice* dev, USBPacket* p)
	{
		MasconState* s = USB_CONTAINER_OF(dev, MasconState, dev);

		switch (p->pid)
		{
			case USB_TOKEN_IN:
				if (p->ep->nr == 1)
				{
					int ret = s->TokenIn(p->buffer_ptr, p->buffer_size);

					if (ret > 0)
						p->actual_length += std::min<u32>(static_cast<u32>(ret), p->buffer_size);
					else
						p->status = ret;
				}
				else
				{
					goto fail;
				}
				break;
			default:
			fail:
				p->status = USB_RET_STALL;
				break;
		}
	}

	static void mascon_handle_reset(USBDevice* dev)
	{
		MasconState* s = USB_CONTAINER_OF(dev, MasconState, dev);
		s->Reset();
	}

	static void mascon_handle_control(USBDevice* dev, USBPacket* p, int request, int value,
		int index, int length, uint8_t* data)
	{
		if (usb_desc_handle_control(dev, p, request, value, index, length, data) < 0)
			p->status = USB_RET_STALL;
	}

	static void mascon_handle_destroy(USBDevice* dev) noexcept
	{
		MasconState* s = USB_CONTAINER_OF(dev, MasconState, dev);
		delete s;
	}

	USBDevice* MasconDevice::CreateDevice(SettingsInterface& si, u32 port, u32 subtype) const
	{
		MasconState* s = new MasconState(port, static_cast<MasconTypes>(subtype));
		s->desc.full = &s->desc_dev;

		switch (subtype)
		{
			case MT_TYPE2:
				s->desc.str = dct01_desc_strings;
				if (usb_desc_parse_dev(dct01_dev_descriptor, sizeof(dct01_dev_descriptor), s->desc, s->desc_dev) < 0)
					goto fail;
				break;
			case MT_SHINKANSEN:
				s->desc.str = dct02_desc_strings;
				if (usb_desc_parse_dev(dct02_dev_descriptor, sizeof(dct02_dev_descriptor), s->desc, s->desc_dev) < 0)
					goto fail;
				break;

			default:
				goto fail;
		}

		if (usb_desc_parse_config(taito_denshacon_config_descriptor, sizeof(taito_denshacon_config_descriptor), s->desc_dev) < 0)
			goto fail;

		s->dev.speed = USB_SPEED_FULL;
		s->dev.klass.handle_attach = usb_desc_attach;
		s->dev.klass.handle_reset = mascon_handle_reset;
		s->dev.klass.handle_control = mascon_handle_control;
		s->dev.klass.handle_data = mascon_handle_data;
		s->dev.klass.unrealize = mascon_handle_destroy;
		s->dev.klass.usb_desc = &s->desc;
		s->dev.klass.product_desc = s->desc.str[2];

		usb_desc_init(&s->dev);
		usb_ep_init(&s->dev);
		mascon_handle_reset(&s->dev);

		return &s->dev;

	fail:
		mascon_handle_destroy(&s->dev);
		return nullptr;
	}

	float MasconDevice::GetBindingValue(const USBDevice* dev, u32 bind_index) const
	{
		const MasconState* s = USB_CONTAINER_OF(dev, const MasconState, dev);
		return s->GetBindValue(bind_index);
	}

	void MasconDevice::SetBindingValue(USBDevice* dev, u32 bind_index, float value) const
	{
		MasconState* s = USB_CONTAINER_OF(dev, MasconState, dev);
		s->SetBindValue(bind_index, value);
	}

	bool MasconDevice::Freeze(USBDevice* dev, StateWrapper& sw) const
	{
		MasconState* s = USB_CONTAINER_OF(dev, MasconState, dev);

		if (!sw.DoMarker("MasconDevice"))
			return false;

		sw.Do(&s->data.power);
		sw.Do(&s->data.brake);
		return true;
	}

	MasconState::MasconState(u32 port_, MasconTypes type_)
		: port(port_)
		, type(type_)
	{
		Reset();
	}

	MasconState::~MasconState() = default;

	void MasconState::Reset()
	{
		data.power = 0x00;
		data.brake = 0x00;
	}

	static constexpr u32 button_mask(u32 bind_index)
	{
		return (1u << (bind_index - MasconControlID::BUTTONS_OFFSET));
	}

	static constexpr u8 button_at(u8 value, u32 index)
	{
		return value & button_mask(index);
	}

	float MasconState::GetBindValue(u32 bind_index) const
	{
		switch (bind_index)
		{
			case CID_MC_POWER:
				return (static_cast<float>(data.power) / 255.0f);
			case CID_MC_BRAKE:
				return (static_cast<float>(data.brake) / 255.0f);

			case CID_MC_UP:
				return static_cast<float>(data.hat_up);
			case CID_MC_DOWN:
				return static_cast<float>(data.hat_down);
			case CID_MC_LEFT:
				return static_cast<float>(data.hat_left);
			case CID_MC_RIGHT:
				return static_cast<float>(data.hat_right);

			case CID_MC_A:
			case CID_MC_B:
			case CID_MC_C:
			case CID_MC_D:
			case CID_MC_SELECT:
			case CID_MC_START:
			{
				return (button_at(data.buttons, bind_index) != 0u) ? 1.0f : 0.0f;
			}

			default:
				return 0.0f;
		}
	}

	void MasconState::SetBindValue(u32 bind_index, float value)
	{
		switch (bind_index)
		{
			case CID_MC_POWER:
				data.power = static_cast<u32>(std::clamp<long>(std::lroundf(value * 255.0f), 0, 255));
				break;
			case CID_MC_BRAKE:
				data.brake = static_cast<u32>(std::clamp<long>(std::lroundf(value * 255.0f), 0, 255));
				break;

			case CID_MC_UP:
				data.hat_up = static_cast<u8>(std::clamp<long>(std::lroundf(value * 255.0f), 0, 255));
				UpdateHatSwitch();
				break;
			case CID_MC_DOWN:
				data.hat_down = static_cast<u8>(std::clamp<long>(std::lroundf(value * 255.0f), 0, 255));
				UpdateHatSwitch();
				break;
			case CID_MC_LEFT:
				data.hat_left = static_cast<u8>(std::clamp<long>(std::lroundf(value * 255.0f), 0, 255));
				UpdateHatSwitch();
				break;
			case CID_MC_RIGHT:
				data.hat_right = static_cast<u8>(std::clamp<long>(std::lroundf(value * 255.0f), 0, 255));
				UpdateHatSwitch();
				break;

			case CID_MC_A:
			case CID_MC_B:
			case CID_MC_C:
			case CID_MC_D:
			case CID_MC_SELECT:
			case CID_MC_START:
			{
				const u32 mask = button_mask(bind_index);
				if (value >= 0.5f)
					data.buttons |= mask;
				else
					data.buttons &= ~mask;
			}
			break;

			default:
				break;
		}
	}

	void MasconState::UpdateHatSwitch() noexcept
	{
		if (data.hat_up && data.hat_right)
			data.hatswitch = 1;
		else if (data.hat_right && data.hat_down)
			data.hatswitch = 3;
		else if (data.hat_down && data.hat_left)
			data.hatswitch = 5;
		else if (data.hat_left && data.hat_up)
			data.hatswitch = 7;
		else if (data.hat_up)
			data.hatswitch = 0;
		else if (data.hat_right)
			data.hatswitch = 2;
		else if (data.hat_down)
			data.hatswitch = 4;
		else if (data.hat_left)
			data.hatswitch = 6;
		else
			data.hatswitch = 8;
	}

	static u8 dct01_power(u8 value)
	{
		// (N) 0x81	0x6D 0x54 0x3F 0x21	0x00 (P5)
		static std::pair<u8, u8> const notches[] = {
			// { control_in, emulated_out },
			{0xF8, 0x00},
			{0xC8, 0x21},
			{0x98, 0x3F},
			{0x58, 0x54},
			{0x28, 0x6D},
			{0x00, 0x81},
		};

		for (const auto& x : notches)
		{
			if (value >= x.first)
				return x.second;
		}
		return notches[std::size(notches) - 1].second;
	}

	static u8 dct01_brake(u8 value)
	{
		// (NB) 0x79 0x8A 0x94 0x9A 0xA2 0xA8 0xAF 0xB2 0xB5 0xB9 (EB)
		static std::pair<u8, u8> const notches[] = {
			// { control_in, emulated_out },
			{0xF8, 0xB9},
			{0xE6, 0xB5},
			{0xCA, 0xB2},
			{0xAE, 0xAF},
			{0x92, 0xA8},
			{0x76, 0xA2},
			{0x5A, 0x9A},
			{0x3E, 0x94},
			{0x22, 0x8A},
			{0x00, 0x79},
		};

		for (const auto& x : notches)
		{
			if (value >= x.first)
				return x.second;
		}
		return notches[std::size(notches) - 1].second;
	}

	static u8 dct02_power(u8 value)
	{
		// (N) 0x12 0x24 0x36 0x48 0x5A 0x6C 0x7E 0x90 0xA2 0xB4 0xC6 0xD7 0xE9 0xFB (P13)
		static std::pair<u8, u8> const notches[] = {
			// { control_in, emulated_out },
			{0xF7, 0xFB},
			{0xE4, 0xE9},
			{0xD1, 0xD7},
			{0xBE, 0xC6},
			{0xAB, 0xB4},
			{0x98, 0xA2},
			{0x85, 0x90},
			{0x72, 0x7E},
			{0x5F, 0x6C},
			{0x4C, 0x5A},
			{0x39, 0x48},
			{0x26, 0x36},
			{0x13, 0x24},
			{0x00, 0x12},
		};

		for (const auto& x : notches)
		{
			if (value >= x.first)
				return x.second;
		}
		return notches[std::size(notches) - 1].second;
	}
	static u8 dct02_brake(u8 value)
	{
		// (NB) 0x1C 0x38 0x54 0x70 0x8B 0xA7 0xC3 0xDF 0xFB (EB)
		static std::pair<u8, u8> const notches[] = {
			// { control_in, emulated_out },
			{0xF8, 0xFB},
			{0xCA, 0xDF},
			{0xAE, 0xC3},
			{0x92, 0xA7},
			{0x76, 0x8B},
			{0x5A, 0x70},
			{0x3E, 0x54},
			{0x22, 0x38},
			{0x00, 0x1C},
		};

		for (const auto& x : notches)
		{
			if (value >= x.first)
				return x.second;
		}
		return notches[std::size(notches) - 1].second;
	}

#define get_ab(buttons) (button_at(buttons, CID_MC_A) | button_at(buttons, CID_MC_B))
#define swap_cd(buttons) ((button_at(buttons, CID_MC_C) << 1) | (button_at(buttons, CID_MC_D) >> 1))
#define get_ss(buttons) (button_at(buttons, CID_MC_START) | button_at(buttons, CID_MC_SELECT))

	// MasconControlID buttons are laid out in Type 2 ordering, no need to remap.
	constexpr u8 dct01_buttons(u8 buttons) { return buttons; }
	constexpr u8 dct02_buttons(u8 buttons)
	{
		return ((get_ab(buttons) << 2) | (swap_cd(buttons) >> 2) | get_ss(buttons));
	}

	int MasconState::TokenIn(u8* buf, int len)
	{
		UpdateHatSwitch();

		switch (type)
		{
			case MT_TYPE2:
				buf[0] = 0x1;
				buf[1] = dct01_brake(data.brake);
				buf[2] = dct01_power(data.power);
				buf[3] = 0xFF; // Button C doubles as horn, skip.
				buf[4] = data.hatswitch & 0x0F;
				buf[5] = dct01_buttons(data.buttons);
				len = 6;
				break;
			case MT_SHINKANSEN:
				buf[0] = dct02_brake(data.brake);
				buf[1] = dct02_power(data.power);
				buf[2] = 0xFF; // Button C doubles as horn, skip.
				buf[3] = data.hatswitch & 0x0F;
				buf[4] = dct02_buttons(data.buttons);
				buf[5] = 0;
				len = 6;
				break;
			default:
				return USB_RET_IOERROR;
		}

		DbgCon.WriteLn("Mascon - Type: %d Power: %02x (raw: %02x) Brake: %02x (raw: %02x) Buttons: %02x buflen: %d",
			type, buf[2], data.power, buf[1], data.brake, buf[5], len);

		return len;
	}
} // namespace usb_pad