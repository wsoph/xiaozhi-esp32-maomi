#pragma once

class MaomiVariant {
public:
    bool Initialize();
    bool IsEnabled() const;

private:
    bool enabled_ = false;
};
