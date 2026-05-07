#include "RuntimeWidget.h"

namespace wasabiq::maki {

QUuid RuntimeWidget::scriptObjectGuid() const {
    if (!widget) return {};
    using Wasabi::WidgetType;
    switch (widget->type) {
    case WidgetType::Group:        return kGroupGuid();
    case WidgetType::GroupRef:     return kGroupGuid();
    case WidgetType::Button:       return kButtonGuid();
    case WidgetType::ToggleButton: return kButtonGuid();
    case WidgetType::Slider:       return kSliderGuid();
    case WidgetType::Text:         return kTextGuid();
    case WidgetType::Layer:
    case WidgetType::Vis:
    case WidgetType::Status:
    case WidgetType::ProgressGrid:
    default:                       return kLayerGuid();
    }
}

} // namespace wasabiq::maki
