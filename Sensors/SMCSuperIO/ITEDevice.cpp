//
//  ITEDevice.cpp
//
//  Sensors implementation for ITE SuperIO device
//
//  Based on https://github.com/kozlek/HWSensors/blob/master/SuperIOSensors/IT87xxSensors.cpp
//  @author joedm.
//

#include "ITEDevice.hpp"
#include "SMCSuperIO.hpp"
#include "Devices.hpp"

namespace ITE {
	uint8_t FAN_PWM_CTRL_REG[ITE_MAX_TACHOMETER_COUNT];
	uint8_t _initialFanPwmControl[ITE_MAX_TACHOMETER_COUNT];
	uint8_t _initialFanOutputModeEnabled[ITE_MAX_TACHOMETER_COUNT];
	uint8_t _initialFanPwmControlExt[ITE_MAX_TACHOMETER_COUNT];
	bool _restoreDefaultFanPwmControlRequired[ITE_MAX_TACHOMETER_COUNT];
	bool _hasExtReg;

	uint16_t ITEDevice::tachometerRead8bit(uint8_t index) {
		if (index > 2) {
			// devices using this reading routine cannot have more than 3 fans
			return 0;
		}
		uint16_t value = readByte(ITE_FAN_TACHOMETER_REG[index]);
		// bits 0-2 - FAN1, bits 3-5 - FAN2, bit 6 - FAN3, bit 7 - reserved
		uint8_t divisorByte = readByte(ITE_FAN_TACHOMETER_DIVISOR_REGISTER) & 0x7F;
		uint8_t divisorShift = (divisorByte >> (3 * index)) & 0x7;
		if (index == 2) {
			// bit 6: set -> divisor = 8, unset -> divisor = 2
			divisorShift = divisorShift == 0 ? 1 : 3;
		}
		uint8_t divisor = 1 << divisorShift;
		return value > 0 && value < 0xff ? 1.35e6f / (value * divisor) : 0;
	}

	uint16_t ITEDevice::tachometerRead(uint8_t index) {
		uint16_t value = readByte(ITE_FAN_TACHOMETER_REG[index]);
		value |= readByte(ITE_FAN_TACHOMETER_EXT_REG[index]) << 8;
		return value > 0x3f && value < 0xffff ? (1.35e6f + value) / (value * 2) : 0;
	}

	void ITEDevice::tachometerWrite(uint8_t index, uint8_t value, bool enabled) {
		if (enabled) {
			tachometerSaveDefault(index);

			if (index < 3 && !_initialFanOutputModeEnabled[index])
				writeByte(FAN_MAIN_CTRL_REG, readByte(FAN_MAIN_CTRL_REG) | (1 << index));

			if (_hasExtReg)
			{
				if (strcmp(getModelName(), "ITE IT8689E"))
					writeByte(FAN_PWM_CTRL_REG[index], 0x7F);
				else
					writeByte(FAN_PWM_CTRL_REG[index], _initialFanPwmControl[index] & 0x7F);

				writeByte(FAN_PWM_CTRL_EXT_REG[index], value);
			}
			else
				writeByte(FAN_PWM_CTRL_REG[index], (value >> 1));

		} else
			tachometerRestoreDefault(index);
	}
	void ITEDevice::tachometerSaveDefault(uint8_t index) {
		if (!_restoreDefaultFanPwmControlRequired[index]) {
			_initialFanPwmControl[index] = readByte(FAN_PWM_CTRL_REG[index]);

			if (index < 3)
				_initialFanOutputModeEnabled[index] = readByte(FAN_MAIN_CTRL_REG); // Save default control reg value.

			if (true)
				_initialFanPwmControlExt[index] = readByte(FAN_PWM_CTRL_EXT_REG[index]);

			_restoreDefaultFanPwmControlRequired[index] = true;
		}
	}

	void ITEDevice::tachometerRestoreDefault(uint8_t index) {
		if (_restoreDefaultFanPwmControlRequired[index]) {
			writeByte(FAN_PWM_CTRL_REG[index], _initialFanPwmControl[index]);

			if (index < 3) {
				uint8_t value = readByte(FAN_MAIN_CTRL_REG);

				bool isEnabled = (value & (1 << index)) != 0;
				if (isEnabled != _initialFanOutputModeEnabled[index])
					writeByte(FAN_MAIN_CTRL_REG, value ^ (1 << index));
			}

			if (true)
				writeByte(FAN_PWM_CTRL_EXT_REG[index], _initialFanPwmControlExt[index]);

			_restoreDefaultFanPwmControlRequired[index] = false;
		}
	}


	uint16_t ITEDevice::tachometerReadEC(uint8_t index) {
		uint16_t value = readByteEC(ITE_EC_FAN_TACHOMETER_REG[index]);
		value |= readByteEC(ITE_EC_FAN_TACHOMETER_EXT_REG[index]) << 8;
		// FIXME: Need to think on this calculation.
		return value > 0x3f && value < 0xffff ? (1.35e6f + value) / (value * 2) : 0;
	}

	float ITEDevice::voltageRead(uint8_t index) {
		uint8_t v = readByte(ITE_VOLTAGE_REG[index]);
		return static_cast<float>(v) * 0.012f;
	}

	float ITEDevice::voltageReadOld(uint8_t index) {
		uint8_t v = readByte(ITE_VOLTAGE_REG[index]);
		return static_cast<float>(v) * 0.016f;
	}

	uint8_t ITEDevice::readByte(uint8_t reg) {
		uint16_t address = getDeviceAddress();
		::outb(address + ITE_ADDRESS_REGISTER_OFFSET, reg);
		return ::inb(address + ITE_DATA_REGISTER_OFFSET);
	}
	
	void ITEDevice::writeByte(uint8_t reg, uint8_t value) {
		uint16_t address = getDeviceAddress();
		::outb(address + ITE_ADDRESS_REGISTER_OFFSET, reg);
		::outb(address + ITE_DATA_REGISTER_OFFSET, value);
	}

	uint8_t ITEDevice::readByteEC(uint16_t addr) {
		auto addrPort = getDevicePort();
		auto dataPort = addr + 1;

		::outb(addrPort, ITE_I2EC_D2ADR_REG);
		::outb(dataPort, ITE_I2EC_ADDR_H);
		::outb(addrPort, ITE_I2EC_D2DAT_REG);
		::outb(dataPort, (addr >> 8) & 0xFF);

		::outb(addrPort, ITE_I2EC_D2ADR_REG);
		::outb(dataPort, ITE_I2EC_ADDR_L);
		::outb(addrPort, ITE_I2EC_D2DAT_REG);
		::outb(dataPort, addr & 0xFF);

		::outb(addrPort, ITE_I2EC_D2ADR_REG);
		::outb(dataPort, ITE_I2EC_DATA);
		::outb(addrPort, ITE_I2EC_D2DAT_REG);
		return ::inb(dataPort);
	}

	void ITEDevice::writeByteEC(uint16_t addr, uint8_t value) {
		auto addrPort = getDevicePort();
		auto dataPort = addr + 1;

		::outb(addrPort, ITE_I2EC_D2ADR_REG);
		::outb(dataPort, ITE_I2EC_ADDR_H);
		::outb(addrPort, ITE_I2EC_D2DAT_REG);
		::outb(dataPort, (addr >> 8) & 0xFF);

		::outb(addrPort, ITE_I2EC_D2ADR_REG);
		::outb(dataPort, ITE_I2EC_ADDR_L);
		::outb(addrPort, ITE_I2EC_D2DAT_REG);
		::outb(dataPort, addr & 0xFF);

		::outb(addrPort, ITE_I2EC_D2ADR_REG);
		::outb(dataPort, ITE_I2EC_DATA);
		::outb(addrPort, ITE_I2EC_D2DAT_REG);
		::outb(dataPort, value);
	}
	
    void ITEDevice::updateTargets() {
		// Update target speeds
		for (uint8_t index = 0; index < getTachometerCount(); index++) {
			DBGLOG("ssio", "ITEDevice Fan %u RPM %d Manual %u", index, getTargetValue(index), getManualValue(index));

			ITEDevice::tachometerWrite(index, (getTargetValue(index) / 3200.00) * 0xff, true);
		}
    }
	void ITEDevice::setupKeys(VirtualSMCAPI::Plugin &vsmcPlugin) {
		VirtualSMCAPI::addKey(KeyFNum, vsmcPlugin.data,
			VirtualSMCAPI::valueWithUint8(getTachometerCount(), nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
		for (uint8_t index = 0; index < getTachometerCount(); ++index) {
			// Current speed
			VirtualSMCAPI::addKey(KeyF0Ac(index), vsmcPlugin.data,
				VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new TachometerKey(getSmcSuperIO(), this, index), SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));
			// Min speed
			VirtualSMCAPI::addKey(KeyF0Mn(index), vsmcPlugin.data,
				VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new MinKey(getSmcSuperIO(), this, index), SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));
			// Max speed
			VirtualSMCAPI::addKey(KeyF0Mx(index), vsmcPlugin.data,
				VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new MaxKey(getSmcSuperIO(), this, index), SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));

			if (getLdn() != EC_ENDPOINT) {
				// No idea why but uncommenting this messes up the SMC Fan keys
				// Enable manual control
				//VirtualSMCAPI::addKey(KeyF0Md(index), vsmcPlugin.data,
				//	VirtualSMCAPI::valueWithUint8(0, new ManualKey(getSmcSuperIO(), this, index), SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));

				// Target speed
				VirtualSMCAPI::addKey(KeyF0Tg(index), vsmcPlugin.data,
									  VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new TargetKey(getSmcSuperIO(), this, index), SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));
			}
		}
	}

	/**
	 *  Device factory helper
	 */
	SuperIODevice* ITEDevice::probePort(i386_ioport_t port, SMCSuperIO* sio) {
		enter(port);
		uint16_t id = listenPortWord(port, SuperIOChipIDRegister);
		DBGLOG("ssio", "ITEDevice probing device on 0x%04X, id=0x%04X", port, id);
		SuperIODevice *detectedDevice = createDeviceITE(id);
		if (detectedDevice) {
			uint16_t address = 0;
			uint8_t ldn = detectedDevice->getLdn();
			DBGLOG("ssio", "ITEDevice detected %s, starting address sanity checks on ldn 0x%02X", detectedDevice->getModelName(), ldn);
			if (ldn != EC_ENDPOINT) {
				selectLogicalDevice(port, detectedDevice->getLdn());
				IOSleep(10);
				activateLogicalDevice(port);
				address = listenPortWord(port, SuperIOBaseAddressRegister);
				IOSleep(10);
				uint16_t verifyAddress = listenPortWord(port, SuperIOBaseAddressRegister);
				IOSleep(10);

				if (address == 0) {
					DBGLOG("ssio", "ITEDevice address is spurious, retrying on alternate: address = 0x%04X, verifyAddress = 0x%04X", address, verifyAddress);
					address = listenPortWord(port, SuperIOBaseAltAddressRegister);
					IOSleep(10);
					verifyAddress = listenPortWord(port, SuperIOBaseAltAddressRegister);
					IOSleep(10);
				}

				if (address != verifyAddress || address < 0x100 || (address & 0xF007) != 0) {
					DBGLOG("ssio", "ITEDevice address verify check error: address = 0x%04X, verifyAddress = 0x%04X", address, verifyAddress);
					delete detectedDevice;
					return nullptr;
				}
			}

			detectedDevice->initialize(address, port, sio);

			if (ldn == EC_ENDPOINT) {
				DBGLOG("ssio", "ITEDevice has EC %02X %02X %02X",
					   static_cast<ITEDevice *>(detectedDevice)->readByteEC(ITE_EC_GCTRL_BASE + ITE_EC_GCTRL_ECHIPID1),
					   static_cast<ITEDevice *>(detectedDevice)->readByteEC(ITE_EC_GCTRL_BASE + ITE_EC_GCTRL_ECHIPID2),
					   static_cast<ITEDevice *>(detectedDevice)->readByteEC(ITE_EC_GCTRL_BASE + ITE_EC_GCTRL_ECHIPVER));
			} else {
				if (strcmp(detectedDevice->getModelName(), "ITE IT8721F") || strcmp(detectedDevice->getModelName(), "ITE IT8728F") || strcmp(detectedDevice->getModelName(), "ITE IT8665E") || strcmp(detectedDevice->getModelName(), "ITE IT8686E") || strcmp(detectedDevice->getModelName(), "ITE IT8688E") || strcmp(detectedDevice->getModelName(), "ITE IT8689E") ||
					strcmp(detectedDevice->getModelName(), "ITE IT8795E") || strcmp(detectedDevice->getModelName(), "ITE IT8628E") ||
					strcmp(detectedDevice->getModelName(), "ITE IT8625E") || strcmp(detectedDevice->getModelName(), "ITE IT8620E") ||
					strcmp(detectedDevice->getModelName(), "ITE IT8613E") || strcmp(detectedDevice->getModelName(), "ITE IT8792E") ||
					strcmp(detectedDevice->getModelName(), "ITE IT8655E") || strcmp(detectedDevice->getModelName(), "ITE IT8631E"))
				{
					_hasExtReg = true;
				}
				if (strcmp(detectedDevice->getModelName(), "ITE IT8665E") || strcmp(detectedDevice->getModelName(), "ITE IT8625E")) {
					lilu_os_memcpy(&FAN_PWM_CTRL_REG, &FAN_PWM_CTRL_REG_ALT, MAX_TACHOMETER_COUNT);
				} else
					lilu_os_memcpy(&FAN_PWM_CTRL_REG, &FAN_PWM_CTRL_REG_1, MAX_TACHOMETER_COUNT);
			}
		}
		leave(port);

		return detectedDevice;
	}

	/**
	 *  Device factory
	 */
	SuperIODevice* ITEDevice::detect(SMCSuperIO* sio) {
		SuperIODevice* dev = probePort(SuperIOPort2E, sio);
		if (dev != nullptr)
			return dev;
		return probePort(SuperIOPort4E, sio);
	}
	
} // namespace ITE
