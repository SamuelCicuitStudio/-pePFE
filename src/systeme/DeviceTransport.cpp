#include "systeme/DeviceTransport.h"

DeviceTransport* DeviceTransport::inst_ = nullptr;

DeviceTransport* DeviceTransport::Get() {
    // Singleton paresseux : cree lors du premier appel.
    if (!inst_) inst_ = new DeviceTransport();
    return inst_;
}

void DeviceTransport::attach(Device* dev) {
    // Aucun mutex ici : on suppose l'appel pendant l'init (setup).
    dev_ = dev;
}

bool DeviceTransport::start() {
    if (!dev_) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Start;
    return dev_->submitCommand(cmd);
}

bool DeviceTransport::stop() {
    if (!dev_) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Stop;
    return dev_->submitCommand(cmd);
}

bool DeviceTransport::toggle() {
    if (!dev_) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Toggle;
    return dev_->submitCommand(cmd);
}

bool DeviceTransport::clearFault() {
    if (!dev_) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::ClearFault;
    return dev_->submitCommand(cmd);
}

bool DeviceTransport::timedRun(uint32_t seconds) {
    if (!dev_) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::TimedRun;
    // payload : duree en secondes
    cmd.u32 = seconds;
    return dev_->submitCommand(cmd);
}

bool DeviceTransport::setRelay(bool on) {
    if (!dev_) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::SetRelay;
    // payload : etat demande
    cmd.b = on;
    return dev_->submitCommand(cmd);
}

bool DeviceTransport::reset() {
    if (!dev_) return false;
    Device::Command cmd;
    cmd.type = Device::Command::Type::Reset;
    return dev_->submitCommand(cmd);
}

bool DeviceTransport::getSnapshot(SystemSnapshot& out) const {
    if (!dev_) return false;
    // Copie coherente effectuee par Device (sous semaphore interne).
    return dev_->getSnapshot(out);
}

DeviceState DeviceTransport::getState() const {
    if (!dev_) return DeviceState::Off;
    return dev_->getState();
}
