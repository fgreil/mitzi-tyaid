#include <furi.h>                        // Core Flipper Zero API (types, memory, threads)
#include <gui/gui.h>                     // GUI system and canvas drawing functions
#include <gui/elements.h>                // UI elements like button hints
#include <gui/modules/text_input.h>      // Standard text input keyboard module
#include <gui/view_dispatcher.h>         // View management and navigation system
#include <mitzi_tyaid_icons.h>           // Auto-generated header for icons in images/ folder

// Debug tag for logging
#define TAG "TypeAid"

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

#define TEXT_BUFFER_SIZE 256

// ============================================================================
// TYPES AND STRUCTURES
// ============================================================================

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    TextInput* text_input;
    ViewDispatcher* view_dispatcher;
    
    char text_buffer[TEXT_BUFFER_SIZE];
    bool in_text_input;  // Track which screen we're on
	bool keyboard_used;  // Track if keyboard has been opened at least once
} TypeAidApp;

// ============================================================================
// SPLASH SCREEN - DRAW CALLBACK
// ============================================================================

static void splash_draw_callback(Canvas* canvas, void* context) {
    FURI_LOG_D(TAG, "splash_draw_callback: enter");
    TypeAidApp* app = context;
    
    if(!app) {
        FURI_LOG_E(TAG, "splash_draw_callback: app is NULL!");
        return;
    }
    
    canvas_clear(canvas);
    
    // Draw splash icon only on first view (before keyboard is used)
    if(!app->keyboard_used){
        canvas_draw_icon(canvas, 50, 1, &I_splash);
		canvas_set_font_direction(canvas, CanvasDirectionBottomToTop); // Set text rotation to 90 degrees 
		canvas_draw_str(canvas, 128, 55, "f418.eu");		
		canvas_set_font_direction(canvas, CanvasDirectionLeftToRight); // Reset to normal text direction
    }
    // Draw icon and title at the top
    canvas_draw_icon(canvas, 1, 1, &I_icon_10x10);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 12, 1, AlignLeft, AlignTop, "Type Aid v0.1");
    
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    
    
    // Display entered text inside the box (truncated, no scrolling)
    if(strlen(app->text_buffer) > 0) {
		const uint8_t box_x = 0;
		const uint8_t box_y = 16;
		const uint8_t box_width = 128;
		const uint8_t box_height = 35;
		// Draw a box for the text display area
		canvas_draw_frame(canvas, box_x, box_y, box_width, box_height);
		
        const uint8_t text_padding = 2;
        const uint8_t line_height = 10;
        const uint8_t start_y = box_y + text_padding;
        const uint8_t max_lines = 3;
        const uint8_t chars_per_line = 21;        
	
        size_t text_len = strlen(app->text_buffer);
        uint8_t line = 0;
        size_t i = 0;
        
        while(line < max_lines && i < text_len) {
            char line_buffer[chars_per_line + 1];
            size_t chars_to_copy = 0;
            
            while(chars_to_copy < chars_per_line && i < text_len) {
                line_buffer[chars_to_copy] = app->text_buffer[i];
                chars_to_copy++;
                i++;
            }
            line_buffer[chars_to_copy] = '\0';
            
            canvas_draw_str_aligned(
                canvas,
                box_x + text_padding,
                start_y + (line * line_height),
                AlignLeft,
                AlignTop,
                line_buffer
            );
            
            line++;
        }
    } else {
        // Show placeholder text when no input yet
        canvas_draw_str_aligned(canvas, 1, 17, AlignLeft, AlignTop, "Just another");
        canvas_draw_str_aligned(canvas, 1, 26, AlignLeft, AlignTop, "keyboard?");
    }	
	

    // Draw navigation hints at bottom
    elements_button_center(canvas, "OK");
	canvas_draw_icon(canvas, 1, 55, &I_back);
	canvas_draw_str_aligned(canvas, 11, 63, AlignLeft, AlignBottom, "Exit");	
    
    FURI_LOG_D(TAG, "splash_draw_callback: exit");
}

// ============================================================================
// SPLASH SCREEN - INPUT CALLBACK
// ============================================================================

static void splash_input_callback(InputEvent* input_event, void* context) {
    TypeAidApp* app = context;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

// ============================================================================
// TEXT INPUT - CALLBACK
// ============================================================================

static void text_input_callback(void* context) {
    FURI_LOG_I(TAG, "text_input_callback: text entered");
    TypeAidApp* app = context;
    
    if(!app) {
        FURI_LOG_E(TAG, "text_input_callback: app is NULL!");
        return;
    }
    
    FURI_LOG_I(TAG, "Text entered: '%s'", app->text_buffer);
    
    // Stop the view dispatcher to return to main loop
    view_dispatcher_stop(app->view_dispatcher);
}

// ============================================================================
// APP LIFECYCLE - ALLOCATION
// ============================================================================

static TypeAidApp* type_aid_app_alloc() {
    FURI_LOG_I(TAG, "=== App allocation started ===");
    
    TypeAidApp* app = malloc(sizeof(TypeAidApp));
    memset(app, 0, sizeof(TypeAidApp));
    
    FURI_LOG_D(TAG, "Opening GUI");
    app->gui = furi_record_open(RECORD_GUI);
    
    FURI_LOG_D(TAG, "Creating event queue");
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    
    FURI_LOG_D(TAG, "Creating viewport for splash");
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, splash_draw_callback, app);
    view_port_input_callback_set(app->view_port, splash_input_callback, app);
    
    FURI_LOG_D(TAG, "Creating view dispatcher");
    app->view_dispatcher = view_dispatcher_alloc();
    
    FURI_LOG_D(TAG, "Creating text input");
    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Enter your text:");
    text_input_set_result_callback(
        app->text_input,
        text_input_callback,
        app,
        app->text_buffer,
        TEXT_BUFFER_SIZE,
        false
    );
    
    // Add text input to view dispatcher
    view_dispatcher_add_view(app->view_dispatcher, 1, text_input_get_view(app->text_input));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    FURI_LOG_D(TAG, "Adding splash viewport to GUI");
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    
    app->in_text_input = false;
    app->keyboard_used = false;  // Initially, keyboard hasn't been used
    FURI_LOG_I(TAG, "=== App allocation complete ===");
    return app;
}

// ============================================================================
// APP LIFECYCLE - CLEANUP
// ============================================================================

static void type_aid_app_free(TypeAidApp* app) {
    FURI_LOG_I(TAG, "=== App cleanup started ===");
    
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    
    view_dispatcher_remove_view(app->view_dispatcher, 1);
    text_input_free(app->text_input);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_message_queue_free(app->event_queue);
    furi_record_close(RECORD_GUI);
    free(app);
    
    FURI_LOG_I(TAG, "=== App cleanup complete ===");
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

int32_t type_aid_main(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "App TYAID starting");
    TypeAidApp* app = type_aid_app_alloc();
    InputEvent event;
    
    FURI_LOG_I(TAG, "Entering main event loop"); // --------------------------
    while(1) {
        if(app->in_text_input) {
            // We're waiting for text input to finish
            furi_delay_ms(100); 
        } else {
            // Normal splash screen event handling
            if(furi_message_queue_get(app->event_queue, &event, 100) == FuriStatusOk) {
                FURI_LOG_D(TAG, "Event: type=%d key=%d", event.type, event.key);
                
                if(event.type == InputTypeShort || event.type == InputTypeLong) {
                    if(event.key == InputKeyBack) {
                        FURI_LOG_I(TAG, "Back pressed, exiting");
                        break;
                    }
                    else if(event.key == InputKeyOk) {
                        FURI_LOG_I(TAG, "OK pressed, showing text input");
                        app->keyboard_used = true; // Flag that keyboard has been used at least once
                        gui_remove_view_port(app->gui, app->view_port); // Remove splash screen
                        
                        // Show text input
                        app->in_text_input = true;
                        view_dispatcher_switch_to_view(app->view_dispatcher, 1);
                        view_dispatcher_run(app->view_dispatcher);
                        
                        // Text input finished, show splash again
                        FURI_LOG_I(TAG, "Text input closed, returning to splash");
                        app->in_text_input = false;
                        gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
                        view_port_update(app->view_port);
                    }
                }
            }
            
            view_port_update(app->view_port);
        }
    }
    FURI_LOG_I(TAG, "Cleaning up");
    type_aid_app_free(app);
    FURI_LOG_I(TAG, "App exiting");
    return 0;
}