#include "ui.hpp"

#include "kettlelogic/app/planning.hpp"

#include <algorithm>
#include <cmath>

namespace kl::ui {

namespace {

void InvokeCallback(gpointer, gpointer data) {
    (*static_cast<Callback*>(data))();
}

void InvokeAction(gpointer, GVariant*, gpointer data) {
    (*static_cast<Callback*>(data))();
}

void InvokeNotify(gpointer, GParamSpec*, gpointer data) {
    (*static_cast<Callback*>(data))();
}

void FreeCallback(gpointer data, GClosure*) {
    delete static_cast<Callback*>(data);
}

std::string Str(std::string_view text) {
    return std::string(text);
}

// Text for a label that reads markup, with nothing in it taken for markup.
std::string Escaped(std::string_view text) {
    char* escaped = g_markup_escape_text(text.data(), static_cast<gssize>(text.size()));
    std::string result = escaped;
    g_free(escaped);
    return result;
}

void AddClass(GtkWidget* widget, const char* name) {
    gtk_widget_add_css_class(widget, name);
}

}  // namespace

void ApplyAppearance(std::string_view appearance) {
    const AdwColorScheme scheme = appearance == "light"  ? ADW_COLOR_SCHEME_FORCE_LIGHT
                                  : appearance == "dark" ? ADW_COLOR_SCHEME_FORCE_DARK
                                                         : ADW_COLOR_SCHEME_DEFAULT;
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), scheme);
}

void Later(Callback work) {
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer data) -> gboolean {
            std::unique_ptr<Callback> work(static_cast<Callback*>(data));
            (*work)();
            return G_SOURCE_REMOVE;
        },
        new Callback(std::move(work)), nullptr);
}

void On(gpointer instance, const char* signal, Callback callback) {
    g_signal_connect_data(instance, signal, G_CALLBACK(InvokeCallback), new Callback(std::move(callback)), FreeCallback,
                          static_cast<GConnectFlags>(0));
}

void OnAction(GSimpleAction* action, Callback callback) {
    g_signal_connect_data(action, "activate", G_CALLBACK(InvokeAction), new Callback(std::move(callback)), FreeCallback,
                          static_cast<GConnectFlags>(0));
}

void OnNotify(gpointer instance, const char* property, Callback callback) {
    const std::string name = std::string("notify::") + property;
    g_signal_connect_data(instance, name.c_str(), G_CALLBACK(InvokeNotify), new Callback(std::move(callback)),
                          FreeCallback, static_cast<GConnectFlags>(0));
}

// MARK: Text

GtkWidget* Text(std::string_view text, Kind kind) {
    GtkWidget* label = gtk_label_new(Str(text).c_str());
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    switch (kind) {
        case Kind::body: break;
        case Kind::strong: AddClass(label, "heading"); break;
        case Kind::caption:
            AddClass(label, "caption");
            AddClass(label, "dim-label");
            break;
        case Kind::secondary: AddClass(label, "dim-label"); break;
        case Kind::title: AddClass(label, "title-2"); break;
        case Kind::subtitle: AddClass(label, "title-3"); break;
        case Kind::mono: AddClass(label, "monospace"); break;
        case Kind::error: AddClass(label, "error"); break;
        case Kind::success: AddClass(label, "success"); break;
    }
    return label;
}

void SetText(GtkWidget* label, std::string_view text) {
    gtk_label_set_text(GTK_LABEL(label), Str(text).c_str());
}

std::vector<Choice> Choices(const std::vector<std::string>& values) {
    std::vector<Choice> out;
    out.reserve(values.size());
    for (const std::string& value : values) out.push_back({value, value});
    return out;
}

// MARK: Buttons

GtkWidget* Button(std::string_view label, Callback onClick, bool accent) {
    GtkWidget* button = gtk_button_new_with_label(Str(label).c_str());
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    if (accent) AddClass(button, "suggested-action");
    On(button, "clicked", std::move(onClick));
    return button;
}

GtkWidget* IconButton(const char* icon, std::string_view tooltip, Callback onClick) {
    GtkWidget* button = gtk_button_new_from_icon_name(icon);
    AddClass(button, "flat");
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    SetTip(button, tooltip);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, Str(tooltip).c_str(), -1);
    On(button, "clicked", std::move(onClick));
    return button;
}

GtkWidget* IconLabelButton(const char* icon, std::string_view label, std::string_view tooltip, Callback onClick) {
    GtkWidget* content = adw_button_content_new();
    adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(content), icon);
    adw_button_content_set_label(ADW_BUTTON_CONTENT(content), Str(label).c_str());
    GtkWidget* button = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    SetTip(button, tooltip);
    On(button, "clicked", std::move(onClick));
    return button;
}

GtkWidget* LinkButton(std::string_view label, Callback onClick) {
    GtkWidget* button = gtk_button_new_with_label(Str(label).c_str());
    AddClass(button, "link");
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    GtkWidget* child = gtk_button_get_child(GTK_BUTTON(button));
    if (GTK_IS_LABEL(child)) {
        gtk_label_set_xalign(GTK_LABEL(child), 0);
        gtk_label_set_wrap(GTK_LABEL(child), TRUE);
    }
    On(button, "clicked", std::move(onClick));
    return button;
}

// MARK: Fields

namespace {

GtkWidget* NewEntry(std::string_view text, double width, bool rightAligned) {
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), Str(text).c_str());
    gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);
    if (width > 0) {
        gtk_editable_set_width_chars(GTK_EDITABLE(entry), 1);
        gtk_widget_set_size_request(entry, static_cast<int>(width), -1);
    } else {
        gtk_widget_set_hexpand(entry, TRUE);
    }
    if (rightAligned) gtk_editable_set_alignment(GTK_EDITABLE(entry), 1.0f);
    return entry;
}

}  // namespace

GtkWidget* Field(std::string_view text, std::function<void(std::string)> commit, double width, bool rightAligned,
                 bool commitOnEnter) {
    GtkWidget* entry = NewEntry(text, width, rightAligned);

    // What the field held when it was last committed, so an unchanged field commits nothing.
    auto committed = std::make_shared<std::string>(text);
    auto run = [entry, committed, commit = std::move(commit)] {
        std::string now = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (now == *committed) return;
        *committed = now;
        if (commit) commit(std::move(now));
    };
    GtkEventController* focus = gtk_event_controller_focus_new();
    On(focus, "leave", run);
    gtk_widget_add_controller(entry, focus);
    if (commitOnEnter) On(entry, "activate", run);
    return entry;
}

GtkWidget* LiveField(std::string_view text, std::function<void(std::string)> changed, double width,
                     bool rightAligned) {
    GtkWidget* entry = NewEntry(text, width, rightAligned);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    On(entry, "changed", [entry, changed = std::move(changed)] {
        if (changed) changed(gtk_editable_get_text(GTK_EDITABLE(entry)));
    });
    return entry;
}

GtkWidget* MultilineField(std::string_view text, std::function<void(std::string)> commit, double minHeight) {
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 10);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, Str(text).c_str(), -1);

    auto committed = std::make_shared<std::string>(text);
    auto run = [buffer, committed, commit = std::move(commit)] {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        char* raw = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        std::string now = raw;
        g_free(raw);
        if (now == *committed) return;
        *committed = now;
        if (commit) commit(std::move(now));
    };
    GtkEventController* focus = gtk_event_controller_focus_new();
    On(focus, "leave", run);
    gtk_widget_add_controller(view, focus);

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), static_cast<int>(minHeight));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    GtkWidget* frame = gtk_frame_new(nullptr);
    gtk_frame_set_child(GTK_FRAME(frame), scroller);
    gtk_widget_set_hexpand(frame, TRUE);
    return frame;
}

// MARK: Pickers

GtkWidget* Picker(const std::vector<Choice>& choices, std::string_view selected,
                  std::function<void(std::string)> onChange, double width) {
    GtkStringList* model = gtk_string_list_new(nullptr);
    guint index = GTK_INVALID_LIST_POSITION;
    for (std::size_t i = 0; i < choices.size(); ++i) {
        gtk_string_list_append(model, choices[i].label.c_str());
        if (choices[i].value == selected) index = static_cast<guint>(i);
    }
    GtkWidget* dropdown = gtk_drop_down_new(G_LIST_MODEL(model), nullptr);
    gtk_widget_set_valign(dropdown, GTK_ALIGN_CENTER);
    if (choices.size() > 12) {
        GtkExpression* expression = gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, nullptr, "string");
        gtk_drop_down_set_expression(GTK_DROP_DOWN(dropdown), expression);
        gtk_expression_unref(expression);
        gtk_drop_down_set_enable_search(GTK_DROP_DOWN(dropdown), TRUE);
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), index);
    if (width > 0) gtk_widget_set_size_request(dropdown, static_cast<int>(width), -1);

    // The change is reported only when the user makes it: the selection was set above, before anyone listened.
    auto values = std::make_shared<std::vector<std::string>>();
    for (const Choice& choice : choices) values->push_back(choice.value);
    OnNotify(dropdown, "selected", [dropdown, values, onChange = std::move(onChange)] {
        const guint chosen = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
        if (chosen == GTK_INVALID_LIST_POSITION || chosen >= values->size()) return;
        if (onChange) onChange((*values)[chosen]);
    });
    return dropdown;
}

GtkWidget* Switch(bool on, std::function<void(bool)> onChange) {
    GtkWidget* toggle = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(toggle), on);
    gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
    OnNotify(toggle, "active", [toggle, onChange = std::move(onChange)] {
        if (onChange) onChange(gtk_switch_get_active(GTK_SWITCH(toggle)));
    });
    return toggle;
}

GtkWidget* Check(std::string_view label, bool on, std::function<void(bool)> onChange) {
    GtkWidget* check = gtk_check_button_new_with_label(Str(label).c_str());
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), on);
    OnNotify(check, "active", [check, onChange = std::move(onChange)] {
        if (onChange) onChange(gtk_check_button_get_active(GTK_CHECK_BUTTON(check)));
    });
    return check;
}

namespace {

std::string DateLabel(const kettlelogic::app::CivilDate& date) {
    GDateTime* time = g_date_time_new_local(date.year, date.month, date.day, 12, 0, 0);
    if (time == nullptr) return "";
    char* text = g_date_time_format(time, "%x");
    std::string result = text != nullptr ? text : "";
    g_free(text);
    g_date_time_unref(time);
    return result;
}

}  // namespace

GtkWidget* DateField(double referenceSeconds, std::function<void(double)> onChange) {
    using kettlelogic::app::CivilDate;
    auto current = std::make_shared<CivilDate>(kettlelogic::app::civilDateOf(referenceSeconds));

    GtkWidget* calendar = gtk_calendar_new();
    if (GDateTime* time = g_date_time_new_local(current->year, current->month, current->day, 12, 0, 0)) {
        gtk_calendar_select_day(GTK_CALENDAR(calendar), time);
        g_date_time_unref(time);
    }
    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), calendar);
    GtkWidget* button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(button), DateLabel(*current).c_str());
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

    // Clicking a day chooses it. Moving to another month only moves the calendar, so it is the click that counts.
    On(calendar, "day-selected", [calendar, popover, button, current, onChange = std::move(onChange)] {
        if (!gtk_widget_get_mapped(popover)) return;
        GDateTime* time = gtk_calendar_get_date(GTK_CALENDAR(calendar));
        const CivilDate picked{g_date_time_get_year(time), g_date_time_get_month(time), g_date_time_get_day_of_month(time)};
        g_date_time_unref(time);
        if (picked.year == current->year && picked.month == current->month && picked.day == current->day) return;
        *current = picked;
        gtk_menu_button_set_label(GTK_MENU_BUTTON(button), DateLabel(picked).c_str());
        gtk_popover_popdown(GTK_POPOVER(popover));
        if (onChange) onChange(kettlelogic::app::referenceSecondsAtMidday(picked));
    });
    return button;
}

GtkWidget* Spacer(double height) {
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(spacer, -1, static_cast<int>(height));
    return spacer;
}

namespace {

GtkWidget* Stack(GtkOrientation orientation, std::initializer_list<GtkWidget*> items, double spacing) {
    GtkWidget* box = gtk_box_new(orientation, static_cast<int>(spacing));
    for (GtkWidget* item : items) {
        if (orientation == GTK_ORIENTATION_HORIZONTAL) {
            gtk_widget_set_valign(item, GTK_ALIGN_CENTER);
            // A label beside other controls keeps to one line, since a row has no room to wrap it.
            if (GTK_IS_LABEL(item)) gtk_label_set_wrap(GTK_LABEL(item), FALSE);
            gtk_box_append(GTK_BOX(box), item);
        } else {
            Append(box, item);
        }
    }
    return box;
}

}  // namespace

GtkWidget* HStack(std::initializer_list<GtkWidget*> items, double spacing) {
    return Stack(GTK_ORIENTATION_HORIZONTAL, items, spacing);
}

GtkWidget* VStack(std::initializer_list<GtkWidget*> items, double spacing) {
    return Stack(GTK_ORIENTATION_VERTICAL, items, spacing);
}

GtkWidget* Box(GtkOrientation orientation, double spacing) {
    return gtk_box_new(orientation, static_cast<int>(spacing));
}

void Append(GtkWidget* box, GtkWidget* child) {
    // A line of text in a column uses the width of the column, so it wraps there and not sooner.
    if (GTK_IS_LABEL(child) && gtk_orientable_get_orientation(GTK_ORIENTABLE(box)) == GTK_ORIENTATION_VERTICAL &&
        gtk_widget_get_halign(child) == GTK_ALIGN_START) {
        gtk_widget_set_hexpand(child, TRUE);
    }
    gtk_box_append(GTK_BOX(box), child);
}

void Style(GtkWidget* widget, const char* cssClass) {
    gtk_widget_add_css_class(widget, cssClass);
}

void SetTip(GtkWidget* widget, std::string_view tip) {
    if (tip.empty()) {
        gtk_widget_set_tooltip_text(widget, nullptr);
    } else {
        gtk_widget_set_tooltip_text(widget, Str(tip).c_str());
    }
}

void Enable(GtkWidget* widget, bool enabled) {
    gtk_widget_set_sensitive(widget, enabled);
}

void Placeholder(GtkWidget* entry, std::string_view text) {
    if (GTK_IS_ENTRY(entry)) gtk_entry_set_placeholder_text(GTK_ENTRY(entry), Str(text).c_str());
}

void Margins(GtkWidget* widget, int start, int top, int end, int bottom) {
    gtk_widget_set_margin_start(widget, start);
    gtk_widget_set_margin_top(widget, top);
    gtk_widget_set_margin_end(widget, end);
    gtk_widget_set_margin_bottom(widget, bottom);
}

GtkWidget* HelpLabel(std::string_view title, std::string_view explanation, bool simpleMode) {
    if (simpleMode) {
        return VStack({Text(title), Text(explanation, Kind::caption)}, 2);
    }
    GtkWidget* body = Text(explanation);
    gtk_label_set_max_width_chars(GTK_LABEL(body), 36);
    Margins(body, 12, 12, 12, 12);
    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), body);
    GtkWidget* info = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(info), "dialog-information-symbolic");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(info), popover);
    AddClass(info, "flat");
    AddClass(info, "circular");
    SetTip(info, explanation);
    gtk_accessible_update_property(GTK_ACCESSIBLE(info), GTK_ACCESSIBLE_PROPERTY_LABEL, "Explain this field", -1);
    return HStack({Text(title), info}, 4);
}

// MARK: Menus

namespace {

// The actions of one open menu, by number. The group is attached to the widget that shows the menu.
struct MenuActions {
    GSimpleActionGroup* group = g_simple_action_group_new();
    int next = 0;
    ~MenuActions() { g_object_unref(group); }
};

GMenu* BuildModel(const std::vector<MenuLine>& entries, MenuActions& actions) {
    GMenu* root = g_menu_new();
    GMenu* section = g_menu_new();
    const auto flush = [&] {
        if (g_menu_model_get_n_items(G_MENU_MODEL(section)) > 0) g_menu_append_section(root, nullptr, G_MENU_MODEL(section));
        g_object_unref(section);
        section = g_menu_new();
    };
    for (const MenuLine& entry : entries) {
        if (entry.separator) {
            flush();
            continue;
        }
        if (!entry.children.empty()) {
            GMenu* sub = BuildModel(entry.children, actions);
            g_menu_append_submenu(section, entry.label.c_str(), G_MENU_MODEL(sub));
            g_object_unref(sub);
            continue;
        }
        const std::string name = "n" + std::to_string(actions.next++);
        GSimpleAction* action = g_simple_action_new(name.c_str(), nullptr);
        g_simple_action_set_enabled(action, entry.enabled);
        OnAction(action, entry.action);
        g_action_map_add_action(G_ACTION_MAP(actions.group), G_ACTION(action));
        g_object_unref(action);
        g_menu_append(section, entry.label.c_str(), ("ctx." + name).c_str());
    }
    flush();
    g_object_unref(section);
    return root;
}

void FreeMenuActions(gpointer data) {
    delete static_cast<MenuActions*>(data);
}

}  // namespace

GtkWidget* MenuButton(const char* icon, std::string_view label, std::string_view tooltip, MenuBuilder builder) {
    GtkWidget* button = gtk_menu_button_new();
    if (icon == nullptr) {
        gtk_menu_button_set_label(GTK_MENU_BUTTON(button), Str(label).c_str());
        gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(button), TRUE);
    } else if (label.empty()) {
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), icon);
        AddClass(button, "flat");
    } else {
        GtkWidget* content = adw_button_content_new();
        adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(content), icon);
        adw_button_content_set_label(ADW_BUTTON_CONTENT(content), Str(label).c_str());
        gtk_menu_button_set_child(GTK_MENU_BUTTON(button), content);
    }
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    SetTip(button, tooltip);
    if (!tooltip.empty()) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, Str(tooltip).c_str(), -1);
    }

    auto* build = new MenuBuilder(std::move(builder));
    gtk_menu_button_set_create_popup_func(
        GTK_MENU_BUTTON(button),
        [](GtkMenuButton* self, gpointer data) {
            auto* actions = new MenuActions();
            GMenu* model = BuildModel((*static_cast<MenuBuilder*>(data))(), *actions);
            gtk_menu_button_set_menu_model(self, G_MENU_MODEL(model));
            g_object_unref(model);
            gtk_widget_insert_action_group(GTK_WIDGET(self), "ctx", G_ACTION_GROUP(actions->group));
            g_object_set_data_full(G_OBJECT(self), "kl-menu-actions", actions, FreeMenuActions);
        },
        build, [](gpointer data) { delete static_cast<MenuBuilder*>(data); });
    return button;
}

namespace {

void ShowContextMenu(GtkWidget* widget, const MenuBuilder& builder, double x, double y) {
    auto* actions = new MenuActions();
    GMenu* model = BuildModel(builder(), *actions);
    GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(model));
    g_object_unref(model);
    gtk_widget_insert_action_group(popover, "ctx", G_ACTION_GROUP(actions->group));
    g_object_set_data_full(G_OBJECT(popover), "kl-menu-actions", actions, FreeMenuActions);
    gtk_widget_set_parent(popover, widget);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    const GdkRectangle where{static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &where);
    // The popover is a child of the widget, so it goes when the menu closes.
    On(popover, "closed", [popover] {
        Later([popover] { gtk_widget_unparent(popover); });
    });
    gtk_popover_popup(GTK_POPOVER(popover));
}

}  // namespace

void ContextMenu(GtkWidget* widget, MenuBuilder builder) {
    auto shared = std::make_shared<MenuBuilder>(std::move(builder));

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    struct Press {
        GtkWidget* widget;
        std::shared_ptr<MenuBuilder> builder;
    };
    g_signal_connect_data(
        click, "pressed",
        G_CALLBACK(+[](GtkGestureClick*, int, double x, double y, gpointer data) {
            auto* press = static_cast<Press*>(data);
            ShowContextMenu(press->widget, *press->builder, x, y);
        }),
        new Press{widget, shared}, [](gpointer data, GClosure*) { delete static_cast<Press*>(data); },
        static_cast<GConnectFlags>(0));
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

    GtkGesture* hold = gtk_gesture_long_press_new();
    gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(hold), TRUE);
    g_signal_connect_data(
        hold, "pressed",
        G_CALLBACK(+[](GtkGestureLongPress*, double x, double y, gpointer data) {
            auto* press = static_cast<Press*>(data);
            ShowContextMenu(press->widget, *press->builder, x, y);
        }),
        new Press{widget, shared}, [](gpointer data, GClosure*) { delete static_cast<Press*>(data); },
        static_cast<GConnectFlags>(0));
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(hold));
}

// MARK: Page

Page::Page() {
    scroller_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller_), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroller_, TRUE);
    gtk_widget_set_vexpand(scroller_, TRUE);
    viewport_ = gtk_viewport_new(nullptr, nullptr);
    gtk_viewport_set_scroll_to_focus(GTK_VIEWPORT(viewport_), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller_), viewport_);
}

void Page::Set(GtkWidget* content) {
    gtk_viewport_set_child(GTK_VIEWPORT(viewport_), content);
}

GtkWidget* Page::Content() const {
    return gtk_viewport_get_child(GTK_VIEWPORT(viewport_));
}

void Page::ScrollToTop() {
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_)), 0);
}

void Page::SetMinHeight(int height) {
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller_), height);
}

// MARK: Form

Form::Form() : page_(gtk_box_new(GTK_ORIENTATION_VERTICAL, 18)) {}

void Form::EnsureGroup() {
    if (group_ == nullptr) Section();
}

void Form::Section(std::string_view header, std::string_view footer) {
    group_ = adw_preferences_group_new();
    if (!header.empty()) adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group_), Escaped(header).c_str());
    if (!footer.empty()) adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group_), Escaped(footer).c_str());
    gtk_box_append(GTK_BOX(page_), group_);
}

void Form::SectionWithAction(std::string_view header, std::string_view actionLabel, Callback action,
                             std::string_view footer) {
    Section(header, footer);
    GtkWidget* button = gtk_button_new_with_label(Str(actionLabel).c_str());
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    On(button, "clicked", std::move(action));
    adw_preferences_group_set_header_suffix(ADW_PREFERENCES_GROUP(group_), button);
}

void Form::AddRow(GtkWidget* row) {
    EnsureGroup();
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group_), row);
}

void Form::Labeled(std::string_view label, GtkWidget* control, std::string_view help) {
    GtkWidget* row = adw_action_row_new();
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), Str(label).c_str());
    if (!help.empty()) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), Str(help).c_str());
    gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
    // A readout sits at the right edge on one line, and the label on the left is the one that wraps.
    if (GTK_IS_LABEL(control)) {
        gtk_label_set_wrap(GTK_LABEL(control), FALSE);
        gtk_label_set_xalign(GTK_LABEL(control), 1);
        gtk_widget_set_halign(control, GTK_ALIGN_END);
        gtk_label_set_selectable(GTK_LABEL(control), TRUE);
    }
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), control);
    AddRow(row);
}

void Form::Labeled(GtkWidget* label, GtkWidget* control) {
    GtkWidget* row = adw_action_row_new();
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(label, 6);
    gtk_widget_set_margin_bottom(label, 6);
    adw_action_row_add_prefix(ADW_ACTION_ROW(row), label);
    gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), control);
    AddRow(row);
}

void Form::Wide(GtkWidget* widget) {
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    Margins(widget, 12, 10, 12, 10);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), widget);
    AddRow(row);
}

void Form::Note(std::string_view text, Kind kind) {
    GtkWidget* label = Text(text, kind);
    gtk_widget_set_hexpand(label, TRUE);
    Wide(label);
}

GtkWidget* Form::Build() {
    GtkWidget* clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 780);
    adw_clamp_set_tightening_threshold(ADW_CLAMP(clamp), 600);
    Margins(page_, 16, 20, 16, 28);
    adw_clamp_set_child(ADW_CLAMP(clamp), page_);
    gtk_widget_set_vexpand(clamp, FALSE);
    gtk_widget_set_valign(clamp, GTK_ALIGN_START);
    return clamp;
}

GtkWidget* Form::BuildPanel() {
    return page_;
}

// MARK: Focus

namespace {

void CollectFocusable(GtkWidget* widget, std::vector<GtkWidget*>& out) {
    if (gtk_widget_get_focusable(widget) && gtk_widget_get_can_target(widget) && gtk_widget_get_visible(widget)) {
        out.push_back(widget);
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child != nullptr; child = gtk_widget_get_next_sibling(child)) {
        CollectFocusable(child, out);
    }
}

}  // namespace

void FocusKeeper::Capture(GtkWidget* screen) {
    index_ = -1;
    caret_ = -1;
    if (screen == nullptr) return;
    GtkRoot* root = gtk_widget_get_root(screen);
    if (root == nullptr) return;
    GtkWidget* focus = gtk_root_get_focus(root);
    if (focus == nullptr || !(focus == screen || gtk_widget_is_ancestor(focus, screen))) return;
    std::vector<GtkWidget*> all;
    CollectFocusable(screen, all);
    const auto found = std::find(all.begin(), all.end(), focus);
    if (found == all.end()) return;
    index_ = static_cast<int>(found - all.begin());
    if (GTK_IS_EDITABLE(focus)) caret_ = gtk_editable_get_position(GTK_EDITABLE(focus));
}

void FocusKeeper::Restore(GtkWidget* screen) {
    const int index = index_;
    const int caret = caret_;
    index_ = -1;
    caret_ = -1;
    if (screen == nullptr || index < 0) return;
    std::vector<GtkWidget*> all;
    CollectFocusable(screen, all);
    if (index >= static_cast<int>(all.size())) return;
    GtkWidget* target = all[static_cast<std::size_t>(index)];
    gtk_widget_grab_focus(target);
    if (caret >= 0 && GTK_IS_EDITABLE(target)) gtk_editable_select_region(GTK_EDITABLE(target), caret, caret);
}

// MARK: Dialogs

void Dialog::SetPrimaryEnabled(bool enabled) {
    if (primary_ != nullptr) gtk_widget_set_sensitive(primary_, enabled);
}

void Dialog::SetPrimaryText(std::string_view text) {
    if (primary_ != nullptr) gtk_button_set_label(GTK_BUTTON(primary_), Str(text).c_str());
}

void Dialog::SetSecondary(std::string_view text, Callback action) {
    if (secondary_ == nullptr) return;
    secondaryAction_ = std::move(action);
    gtk_widget_set_visible(secondary_, !text.empty());
    if (!text.empty()) gtk_button_set_label(GTK_BUTTON(secondary_), Str(text).c_str());
}

void Dialog::Close() {
    if (dialog_ != nullptr) {
        adw_dialog_set_can_close(dialog_, TRUE);
        adw_dialog_close(dialog_);
    }
}

GtkWidget* InDialog(GtkWidget* content, int maxHeight) {
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), maxHeight);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);
    return scroller;
}

std::shared_ptr<Dialog> ShowDialog(GtkWidget* parent, const DialogOptions& options, Callback done) {
    auto dialog = std::make_shared<Dialog>();
    dialog->dialog_ = adw_dialog_new();
    AdwDialog* adw = dialog->dialog_;
    adw_dialog_set_title(adw, options.title.c_str());
    // The dialog is as tall as its content, up to the height of the window, and as wide as asked for.
    adw_dialog_set_follows_content_size(adw, TRUE);

    GtkWidget* header = adw_header_bar_new();
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(header), FALSE);
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), FALSE);

    const bool hasPrimary = !options.primaryText.empty();
    const bool hasClose = !options.closeText.empty();
    if (hasClose) {
        GtkWidget* close = gtk_button_new_with_label(options.closeText.c_str());
        On(close, "clicked", [adw] { adw_dialog_close(adw); });
        if (hasPrimary) {
            adw_header_bar_pack_start(ADW_HEADER_BAR(header), close);
        } else {
            adw_header_bar_pack_end(ADW_HEADER_BAR(header), close);
            gtk_widget_add_css_class(close, "suggested-action");
        }
    } else {
        adw_dialog_set_can_close(adw, FALSE);
    }

    std::weak_ptr<Dialog> weak = dialog;
    dialog->secondary_ = gtk_button_new();
    gtk_widget_set_visible(dialog->secondary_, FALSE);
    On(dialog->secondary_, "clicked", [weak] {
        if (auto self = weak.lock(); self && self->secondaryAction_) self->secondaryAction_();
    });
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), dialog->secondary_);

    if (hasPrimary) {
        dialog->primary_ = gtk_button_new_with_label(options.primaryText.c_str());
        gtk_widget_add_css_class(dialog->primary_, "suggested-action");
        gtk_widget_set_sensitive(dialog->primary_, options.primaryEnabled);
        On(dialog->primary_, "clicked", [adw, primary = options.primary] {
            if (primary && !primary()) return;
            adw_dialog_set_can_close(adw, TRUE);
            adw_dialog_close(adw);
        });
        adw_header_bar_pack_end(ADW_HEADER_BAR(header), dialog->primary_);
        adw_dialog_set_default_widget(adw, dialog->primary_);
    }

    GtkWidget* toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), options.content);
    if (options.width > 0) gtk_widget_set_size_request(toolbar, options.width, -1);
    adw_dialog_set_child(adw, toolbar);

    // The dialog and its wrapper hold each other until it closes.
    On(adw, "closed", [dialog, done = std::move(done)]() mutable {
        dialog->dialog_ = nullptr;
        dialog->primary_ = nullptr;
        dialog->secondary_ = nullptr;
        dialog->secondaryAction_ = nullptr;
        if (done) done();
    });
    adw_dialog_present(adw, parent);
    return dialog;
}

void ShowMessage(GtkWidget* parent, std::string_view title, std::string_view message) {
    AdwDialog* dialog = adw_alert_dialog_new(Str(title).c_str(), Str(message).c_str());
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", "OK");
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "ok");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "ok");
    adw_dialog_present(dialog, parent);
}

void ShowConfirm(GtkWidget* parent, std::string_view title, std::string_view message, std::string_view confirmText,
                 Callback confirm) {
    AdwDialog* dialog = adw_alert_dialog_new(Str(title).c_str(), Str(message).c_str());
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel");
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "confirm", Str(confirmText).c_str());
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "confirm", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    g_signal_connect_data(
        dialog, "response",
        G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer data) {
            if (std::string(response) == "confirm") (*static_cast<Callback*>(data))();
        }),
        new Callback(std::move(confirm)), FreeCallback, static_cast<GConnectFlags>(0));
    adw_dialog_present(dialog, parent);
}

}  // namespace kl::ui
