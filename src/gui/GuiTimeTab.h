#pragma once

class ITimeSystem;
class ILocationControl;

namespace GuiTimeTab {
    void render(ITimeSystem& timeSystem, ILocationControl& locationControl);
}
