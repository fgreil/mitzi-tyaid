#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/modules/text_input.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>

// Maximum text length for user input
#define TEXT_BUFFER_SIZE 256

// View IDs for the view dispatcher
typedef enum {
    ViewSplash,
    ViewTextInput,
} AppView;

// App structure to hold all application data
typedef struct {
    ViewDispatcher* view_dispatcher;
    ViewPort* splash_view_port;
    TextInput* text_input;
    
    char text_buffer[TEXT_BUFFER_SIZE];  // Buffer to store entered text
    uint32_t scroll_offset;              // For scrolling text in the box
} SimpleApp;

// ============================================================================
// SPLASH SCREEN
// ============================================================================

// Draw callback for splash screen
static void splash_draw_callback(Canvas* canvas, void* context) {
    SimpleApp* app = context;
    canvas_clear(canvas);
    
    // Draw icon and title at the top
    canvas_draw_icon(canvas, 1, 1, &I_icon_10x10);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 12, 1, AlignLeft, AlignTop, "Simple App");
    
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    
    // Draw a box for the text display area (underneath title)
    const uint8_t box_x = 1;
    const uint8_t box_y = 16;
    const uint8_t box_width = 106;  // Leave room for the rotated date
    const uint8_t box_height = 35;
    
    // Draw the box frame
    canvas_draw_frame(canvas, box_x, box_y, box_width, box_height);
    
    // Display entered text inside the box, or placeholder text
    if(strlen(app->text_buffer) > 0) {
        // Draw the entered text in a scrollable area inside the box
        const uint8_t text_padding = 2;
        const uint8_t line_height = 10;
        const uint8_t start_y = box_y + text_padding;
        const uint8_t max_lines = 3;  // Number of visible lines in the box
        const uint8_t chars_per_line = 21;  // Characters that fit in the box width
        
        size_t text_len = strlen(app->text_buffer);
        
        // Draw text line by line with scrolling
        uint8_t line = 0;
        for(size_t i = app->scroll_offset; i < text_len && line < max_lines; line++) {
            char line_buffer[chars_per_line + 1];
            size_t chars_to_copy = 0;
            
            // Copy characters for this line
            while(chars_to_copy < chars_per_line && (i + chars_to_copy) < text_len) {
                line_buffer[chars_to_copy] = app->text_buffer[i + chars_to_copy];
                chars_to_copy++;
            }
            line_buffer[chars_to_copy] = '\0';
            
            // Draw the line inside the box
            canvas_draw_str_aligned(
                canvas, 
                box_x + text_padding, 
                start_y + (line * line_height), 
                AlignLeft, 
                AlignTop, 
                line_buffer
            );
            
            i += chars_to_copy;
        }
        
        // Show scroll indicators if text is longer than visible area
        if(app->scroll_offset > 0) {
            // Up arrow - can scroll up
            canvas_draw_str(canvas, box_x + box_width - 8, box_y + 8, "^");
        }
        if(app->scroll_offset + (chars_per_line * max_lines) < text_len) {
            // Down arrow - can scroll down
            canvas_draw_str(canvas, box_x + box_width - 8, box_y + box_height - 8, "v");
        }
    } else {
        // Show placeholder text when no input yet
        canvas_draw_str_aligned(canvas, box_x + 3, box_y + 12, AlignLeft, AlignTop, "Just a keyboard?");
        canvas_draw_str_aligned(canvas, box_x + 3, box_y + 22, AlignLeft, AlignTop, "You try it!");
    }
    
    // Draw rotated date text on the right side
    canvas_set_font_direction(canvas, CanvasDirectionBottomToTop);
    canvas_draw_str(canvas, 128, 45, "2025-12");
    canvas_set_font_direction(canvas, CanvasDirectionLeftToRight);
    
    // Draw author and version info
    canvas_draw_str_aligned(canvas, 100, 54, AlignLeft, AlignTop, "F. Greil");
    canvas_draw_str_aligned(canvas, 110, 1, AlignLeft, AlignTop, "v0.1");
    
    // Draw button hints at bottom
    elements_button_center(canvas, "OK");
}

// Input callback for splash screen
static void splash_input_callback(InputEvent* input_event, void* context) {
    SimpleApp* app = context;
    
    if(input_event->type == InputTypeShort || input_event->type == InputTypeRepeat) {
        if(input_event->key == InputKeyOk) {
            // OK button: open keyboard to enter/edit text
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewTextInput);
        }
        else if(input_event->key == InputKeyBack) {
            // Back button: exit app
            view_dispatcher_stop(app->view_dispatcher);
        }
        else if(input_event->key == InputKeyUp) {
            // Scroll up in the text box
            if(app->scroll_offset > 0) {
                app->scroll_offset -= 21;  // Scroll by one line's worth of characters
                if(app->scroll_offset < 0) app->scroll_offset = 0;
                view_port_update(app->splash_view_port);
            }
        }
        else if(input_event->key == InputKeyDown) {
            // Scroll down in the text box
            size_t text_len = strlen(app->text_buffer);
            if(app->scroll_offset + 63 < text_len) {  // 63 = 21 chars * 3 lines
                app->scroll_offset += 21;
                view_port_update(app->splash_view_port);
            }
        }
    }
}

// ============================================================================
// TEXT INPUT (KEYBOARD)
// ============================================================================

// Callback when text input is complete (user pressed OK on keyboard)
static void text_input_callback(void* context) {
    SimpleApp* app = context;
    
    // Reset scroll offset when new text is entered
    app->scroll_offset = 0;
    
    // Return to splash screen to show the entered text
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewSplash);
}

// ============================================================================
// VIEW DISPATCHER CALLBACKS
// ============================================================================

// Custom event callback (not used in this simple app, but required)
static bool navigation_event_callback(void* context) {
    UNUSED(context);
    return false;
}

// Back event callback for the view dispatcher
static uint32_t view_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

// ============================================================================
// APP LIFECYCLE
// ============================================================================

// Allocate and initialize the app
static SimpleApp* simple_app_alloc() {
    SimpleApp* app = malloc(sizeof(SimpleApp));
    
    // Initialize text buffer
    memset(app->text_buffer, 0, TEXT_BUFFER_SIZE);
    app->scroll_offset = 0;
    
    // Create view dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    
    // Set up splash screen view
    app->splash_view_port = view_port_alloc();
    view_port_draw_callback_set(app->splash_view_port, splash_draw_callback, app);
    view_port_input_callback_set(app->splash_view_port, splash_input_callback, app);
    view_dispatcher_add_view(app->view_dispatcher, ViewSplash, view_port_to_view(app->splash_view_port));
    
    // Set up text input (keyboard) view
    app->text_input = text_input_alloc();
    text_input_set_result_callback(
        app->text_input,
        text_input_callback,
        app,
        app->text_buffer,
        TEXT_BUFFER_SIZE,
        true  // Clear text on start
    );
    text_input_set_header_text(app->text_input, "Enter your text:");
    view_set_previous_callback(text_input_get_view(app->text_input), view_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewTextInput, text_input_get_view(app->text_input));
    
    // Attach view dispatcher to GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    
    // Start with splash screen
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewSplash);
    
    return app;
}

// Free app resources
static void simple_app_free(SimpleApp* app) {
    // Remove views from dispatcher
    view_dispatcher_remove_view(app->view_dispatcher, ViewSplash);
    view_dispatcher_remove_view(app->view_dispatcher, ViewTextInput);
    
    // Free views
    view_port_free(app->splash_view_port);
    text_input_free(app->text_input);
    
    // Free view dispatcher
    view_dispatcher_free(app->view_dispatcher);
    
    // Close GUI record
    furi_record_close(RECORD_GUI);
    
    // Free app structure
    free(app);
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

// Main app entry point - matches the entry_point in application.fam
int32_t type_aid_main(void* p) {
    UNUSED(p);
    
    // Allocate app
    SimpleApp* app = simple_app_alloc();
    
    // Run the view dispatcher (blocks until app exits)
    view_dispatcher_run(app->view_dispatcher);
    
    // Clean up and free resources
    simple_app_free(app);
    
    return 0;
}
