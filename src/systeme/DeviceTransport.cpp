#include <DeviceTransport.hpp>

DeviceTransport* DeviceTransport::inst_ = nullptr;

DeviceTransport* DeviceTransport::Get() {
    // Singleton paresseux : cree lors du premier appel.
    if (!inst_) inst_ = new DeviceTransport();
    return inst_;
}

bool DeviceTransport::start() {
    if (!DEVICE) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Start;
    return DEVICE->submitCommand(cmd);
}

bool DeviceTransport::stop() {
    if (!DEVICE) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Stop;
    return DEVICE->submitCommand(cmd);
}

bool DeviceTransport::toggle() {
    if (!DEVICE) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Toggle;
    return DEVICE->submitCommand(cmd);
}

bool DeviceTransport::clearFault() {
    if (!DEVICE) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::ClearFault;
    return DEVICE->submitCommand(cmd);
}

bool DeviceTransport::timedRun(uint32_t seconds) {
    if (!DEVICE) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::TimedRun;
    // payload : duree en secondes
    cmd.u32 = seconds;
    return DEVICE->submitCommand(cmd);
}

bool DeviceTransport::setRelay(bool on) {
    if (!DEVICE) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::SetRelay;
    // payload : etat demande
    cmd.b = on;
    return DEVICE->submitCommand(cmd);
}

bool DeviceTransport::reset() {
    if (!DEVICE) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Reset;
    return DEVICE->submitCommand(cmd);
}

bool DeviceTransport::getSnapshot(SystemSnapshot& out) const {
    if (!DEVICE) return false;
    // Copie coherente effectuee par Device (sous semaphore interne).
    return DEVICE->getSnapshot(out);
}

DeviceState DeviceTransport::getState() const {
    if (!DEVICE) return DeviceState::Off;
    return DEVICE->getState();
}
