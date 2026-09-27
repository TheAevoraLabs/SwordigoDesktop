# HUD/UI Input Blocking Fix (F3 Menu Issue)

## Problem

When the F3 menu is open, the game cannot be clicked. The menu blocks all input to the 3D viewport, making it impossible to interact with the game while the menu is visible.

## Root Cause

The issue is likely in the **input event handling order**:

1. **Current (Broken):** UI/HUD processes events first, consumes them, viewport never sees them
2. **Correct:** Viewport processes events first, UI only handles events it needs

## Solution

### Option 1: Event Propagation (Recommended)

**File:** `src/ruby/viewport/viewport.cpp` (or main window)

```cpp
// In the event handler for the viewport:
bool Viewport::eventFilter(QObject* obj, QEvent* event) {
    // Only handle mouse/keyboard events
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::MouseMove ||
        event->type() == QEvent::KeyPress ||
        event->type() == QEvent::KeyRelease) {
        
        // Check if F3 menu is open
        if (m_f3_menu_open) {
            // Let the menu handle the event first
            if (m_f3_menu->eventFilter(obj, event)) {
                return true;  // Menu consumed the event
            }
        }
        
        // If menu didn't consume it, or menu is closed, handle in viewport
        return handle_viewport_event(event);
    }
    
    return QOpenGLWidget::eventFilter(obj, event);
}
```

### Option 2: Modal Dialog (Alternative)

If the F3 menu is a dialog, make it **non-modal** so the viewport can still receive events:

```cpp
// OLD (blocks input):
m_f3_menu = new F3Menu(this);
m_f3_menu->exec();  // Modal — blocks everything

// NEW (allows input):
m_f3_menu = new F3Menu(this);
m_f3_menu->setWindowModality(Qt::NonModal);
m_f3_menu->show();  // Non-modal — viewport still gets events
```

### Option 3: Input Focus Management

**File:** `src/ruby/viewport/viewport.cpp`

```cpp
void Viewport::on_f3_menu_opened() {
    // Keep focus on viewport, not menu
    this->setFocus();
    m_f3_menu->setFocusPolicy(Qt::NoFocus);
}

void Viewport::on_f3_menu_closed() {
    // Ensure viewport has focus
    this->setFocus();
}
```

## Implementation Steps

### Step 1: Identify the Input Handler

Find where mouse/keyboard events are processed:

```bash
grep -r "mousePressEvent\|keyPressEvent" src/ruby/viewport/
```

### Step 2: Check Event Propagation

Verify that the viewport's event handler is called BEFORE the menu's:

```cpp
// In viewport.cpp:
bool Viewport::mousePressEvent(QMouseEvent* event) {
    qDebug() << "Viewport received mouse press at" << event->pos();
    
    // Check if menu is open
    if (m_f3_menu_open) {
        // Try to let menu handle it
        if (m_f3_menu->handle_event(event)) {
            return true;  // Menu handled it
        }
    }
    
    // Viewport handles it
    return handle_game_input(event);
}
```

### Step 3: Test Input Blocking

```cpp
// Add debug output to verify event flow:
void Viewport::mousePressEvent(QMouseEvent* event) {
    qDebug() << "Viewport::mousePressEvent" << event->pos();
    
    if (m_f3_menu_open) {
        qDebug() << "F3 menu is open";
        if (m_f3_menu->eventFilter(nullptr, event)) {
            qDebug() << "Menu consumed event";
            return;
        }
    }
    
    qDebug() << "Viewport handling event";
    // ... handle game input ...
}
```

### Step 4: Verify F3 Menu Implementation

**File:** `src/ruby/viewport/f3_menu.cpp` (or similar)

```cpp
// Check that the menu's event handler returns false if it doesn't handle the event:
bool F3Menu::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        
        // Check if click is inside menu bounds
        if (this->rect().contains(this->mapFromGlobal(me->globalPos()))) {
            // Click is inside menu, handle it
            return handle_menu_click(me);
        }
    }
    
    // Click is outside menu, don't consume it
    return false;
}
```

## Stencil Mask for UI/HUD

The composite shader uses a stencil mask to exclude UI pixels from post-processing. This is separate from input handling but related:

**File:** `src/ruby/render/fbo_chain.cpp`

```cpp
// When rendering UI/HUD, mark pixels with stencil = 1:
glStencilFunc(GL_ALWAYS, 1, 0xFF);
glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
render_ui_elements();

// When rendering 3D world, mark pixels with stencil = 0:
glStencilFunc(GL_ALWAYS, 0, 0xFF);
glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
render_3d_scene();
```

Then in the composite shader:

```glsl
// Exempt UI pixels from post-processing
if (u_ui_exempt > 0.5 && texture(u_ui_mask, v_uv).g >= 0.002) {
    FragColor = texture(u_scene, v_uv);
    return;
}
```

## Testing

### Test 1: F3 Menu Doesn't Block Input

```cpp
void test_f3_menu_input() {
    // Open F3 menu
    viewport->open_f3_menu();
    
    // Try to click on viewport
    QMouseEvent click(QEvent::MouseButtonPress, QPoint(100, 100),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    viewport->mousePressEvent(&click);
    
    // Verify viewport received the event
    assert(viewport->last_click_pos == QPoint(100, 100));
}
```

### Test 2: F3 Menu Handles Its Own Clicks

```cpp
void test_f3_menu_click() {
    // Open F3 menu
    viewport->open_f3_menu();
    
    // Click inside menu bounds
    QMouseEvent click(QEvent::MouseButtonPress, QPoint(50, 50),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    viewport->mousePressEvent(&click);
    
    // Verify menu handled the click, not viewport
    assert(menu->last_click_pos == QPoint(50, 50));
    assert(viewport->last_click_pos != QPoint(50, 50));
}
```

### Test 3: Clicks Outside Menu Go to Viewport

```cpp
void test_click_outside_menu() {
    // Open F3 menu
    viewport->open_f3_menu();
    
    // Click outside menu bounds
    QMouseEvent click(QEvent::MouseButtonPress, QPoint(500, 500),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    viewport->mousePressEvent(&click);
    
    // Verify viewport handled the click
    assert(viewport->last_click_pos == QPoint(500, 500));
    assert(menu->last_click_pos != QPoint(500, 500));
}
```

## Debugging

### Enable Event Logging

```cpp
// In viewport.cpp:
#define DEBUG_INPUT 1

#if DEBUG_INPUT
#define LOG_INPUT(fmt, ...) qDebug() << QString::asprintf(fmt, ##__VA_ARGS__)
#else
#define LOG_INPUT(fmt, ...)
#endif

bool Viewport::mousePressEvent(QMouseEvent* event) {
    LOG_INPUT("Viewport::mousePressEvent at (%d, %d)", event->x(), event->y());
    
    if (m_f3_menu_open) {
        LOG_INPUT("F3 menu is open, checking if it handles event");
        if (m_f3_menu->eventFilter(nullptr, event)) {
            LOG_INPUT("F3 menu consumed event");
            return true;
        }
        LOG_INPUT("F3 menu did not consume event");
    }
    
    LOG_INPUT("Viewport handling event");
    return handle_game_input(event);
}
```

### Check Event Queue

```cpp
// Verify events are being delivered:
void Viewport::paintGL() {
    // ... render ...
    
    // Check if any events are pending
    if (QApplication::hasPendingEvents()) {
        qDebug() << "Pending events in queue";
    }
}
```

## Related Issues

### UI Bloom Halos

If UI elements have bloom halos around them, this is a separate issue (shader-related):

**Fix:** Use the stencil mask in the bloom extraction shader:

```glsl
// In bloom_extract_remaster.glsl:
float ui_needed = 1.0;
if (u_ui_exempt > 0.5) {
    ui_needed = (texture(u_ui_mask, v_uv).g < 0.002) ? 1.0 : 0.0;
}

vec3 box0 = (d + e + i + j) * 0.25 * ui_needed;
// ... apply to all boxes ...
```

### UI Shadows

If UI elements have shadows, this is also shader-related:

**Fix:** Exempt UI pixels from shadow computation:

```glsl
// In composite_remaster.glsl:
if (u_ui_exempt > 0.5 && texture(u_ui_mask, v_uv).g >= 0.002) {
    FragColor = texture(u_scene, v_uv);
    return;
}
```

## Summary

The F3 menu input blocking issue is likely caused by:

1. **Modal dialog** — Menu blocks all input to viewport
2. **Event consumption** — Menu consumes events without checking if they're inside its bounds
3. **Focus management** — Menu steals focus from viewport

**Solution:**
- Make menu non-modal
- Check event bounds before consuming
- Keep focus on viewport
- Use stencil mask to exclude UI from post-processing

**Testing:**
- Verify F3 menu doesn't block viewport input
- Verify F3 menu handles its own clicks
- Verify clicks outside menu go to viewport
- Verify no UI bloom halos or shadows
