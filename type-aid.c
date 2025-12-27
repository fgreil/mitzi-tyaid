#include <furi.h>                        // Core Flipper Zero API (types, memory, threads)
#include <gui/gui.h>                     // GUI system and canvas drawing functions
#include <gui/elements.h>                // UI elements like button hints
#include <gui/modules/text_input.h>      // Standard text input keyboard module
#include <gui/view_dispatcher.h>         // View management and navigation system
#include <mitzi_tyaid_icons.h>           // Auto-generated header for icons in images/ folder
#include "t9plus.h"					     // a T9 inspired word prediction system

// Debug tag for logging
#define TAG "TypeAid"

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================
#define TEXT_BUFFER_SIZE 256
#define T9_LINE1_OFFSET 16
#define T9_LINE2_OFFSET 20
#define T9_LINE3_OFFSET 20
#define T9_LINE4_OFFSET 30 
static const char* t9_lines[] = {
    "qwertzuiop[]",
    "asdfghjkl'",
    "yxcvbnm,;.:-",
	""
};
static const char* t9_lines_upper[] = {
    "QWERTZUIOP[]",
    "ASDFGHJKL'",
    "YXCVBNM,;.:-",
	""
};

// ============================================================================
// TYPES AND STRUCTURES
// ============================================================================

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    TextInput* text_input;
    ViewDispatcher* view_dispatcher;
    ViewPort* t9_view_port;
    
    char text_buffer[TEXT_BUFFER_SIZE];
    bool in_text_input;  // Track which screen we're on
	bool keyboard_used;  // Track if keyboard has been opened at least once
} TypeAidApp;

// ============================================================================
// T9-MINUS SCREEN - TYPES AND DATA
// ============================================================================
typedef struct {
    uint8_t line;  // 0-3
    int8_t pos;   // position within line (can be -1 for shft button)
} T9Cursor;

static T9Cursor t9_cursor = {0, 0};
static bool shift_locked = false;

// ============================================================================
// T9-MINUS SCREEN - DRAW CALLBACK
// ============================================================================

static void t9_draw_callback(Canvas* canvas, void* context) {
    TypeAidApp* app = context;
    if(!app) {
        return;
    }  
    canvas_clear(canvas);
	// Display current text buffer 
    if(strlen(app->text_buffer) > 0) {
        canvas_draw_str_aligned(canvas, 0, 1, AlignLeft, AlignTop, app->text_buffer);
    }   
	// Draw horizontal lines
	const uint8_t divider_positions[] = {12, 24};
	uint8_t divider_y = 0;
	const size_t num_dividers = sizeof(divider_positions) / sizeof(divider_positions[0]);
	for(size_t i = 0; i < num_dividers; i++) {
		canvas_draw_line(canvas, 0, divider_positions[i], 128, divider_positions[i]);
		divider_y = divider_positions[i];
	}
    // Draw word suggestions between buffer and divider
    if(strlen(app->text_buffer) > 0) {
        char suggestions[T9PLUS_MAX_SUGGESTIONS][T9PLUS_MAX_WORD_LENGTH];
        memset(suggestions, 0, sizeof(suggestions));
        
        uint8_t num_suggestions = t9plus_get_suggestions(app->text_buffer, suggestions, T9PLUS_MAX_SUGGESTIONS);
        FURI_LOG_I(TAG, "Suggestions: %i", num_suggestions);
		
        if(num_suggestions > 0) {
            canvas_set_font(canvas, FontSecondary);
			const uint8_t sugg_y = divider_y - 9;
            
            // Build suggestion line with proper spacing
            FuriString* sugg_str = furi_string_alloc();
            for(uint8_t i = 0; i < num_suggestions; i++) {
                if(i > 0) furi_string_cat(sugg_str, "  ");
                furi_string_cat(sugg_str, suggestions[i]);
            }
            canvas_draw_str(canvas, 2, sugg_y, furi_string_get_cstr(sugg_str));
            furi_string_free(sugg_str);
        }
    }
    
	// Draw the three lines of letters below the last divider
    const uint8_t start_y = divider_y + 10;
    const uint8_t line_spacing = 9;
    const uint8_t char_spacing = 9;
    for(uint8_t line = 0; line < 4; line++) {
        const char* line_str = shift_locked ? t9_lines_upper[line] : t9_lines[line];
        size_t line_len = strlen(line_str);
		uint8_t start_x = 2;
        if(line == 0) start_x += T9_LINE1_OFFSET;
        if(line == 1) start_x += T9_LINE2_OFFSET;        
        // Draw "shft" button for line 2
        if(line == 2) {
            uint8_t shft_x = start_x;
            uint8_t shft_y = start_y + (line * line_spacing);
            
            bool is_shft_cursor = (t9_cursor.line == 2 && t9_cursor.pos == -1);
            if(is_shft_cursor || shift_locked) {
                canvas_set_font(canvas, FontPrimary);
            } else {
                canvas_set_font(canvas, FontSecondary);
            }
            canvas_draw_str(canvas, shft_x, shft_y, "shft");
            
            // Adjust start_x for the rest of line 2
            start_x += T9_LINE3_OFFSET;  // Space for "shft" + gap
        }
		// Draw "[space]" button for line 3
        if(line == 3) {
            uint8_t space_x = T9_LINE4_OFFSET;
            uint8_t space_y = start_y + (line * line_spacing);
            
            bool is_space_cursor = (t9_cursor.line == 3 && t9_cursor.pos == -1);
            if(is_space_cursor) {
                canvas_set_font(canvas, FontPrimary);
            } else {
                canvas_set_font(canvas, FontSecondary);
            }
            canvas_draw_str(canvas, space_x, space_y, "[ space ]");
			canvas_set_font(canvas, FontSecondary);
        }
		
        for(size_t i = 0; i < line_len; i++) {
            uint8_t x = start_x + (i * char_spacing);
            uint8_t y = start_y + (line * line_spacing);
            
            // Set bold font for cursor position
            bool is_cursor = (line == t9_cursor.line && (int8_t)i == t9_cursor.pos);
            
            if(is_cursor) {
                canvas_set_font(canvas, FontPrimary);
            } else {
                canvas_set_font(canvas, FontSecondary);
            }
            
            char single_char[2] = {line_str[i], '\0'};
            canvas_draw_str(canvas, x, y, single_char);
        }
    }
    
    // Draw navigation hints at bottom
    canvas_draw_icon(canvas, 1, 55, &I_back);
    canvas_draw_str_aligned(canvas, 11, 63, AlignLeft, AlignBottom, "Exit");
}

// ============================================================================
// T9-MINUS SCREEN - INPUT CALLBACK
// ============================================================================

static void t9_input_callback(InputEvent* input_event, void* context) {
    TypeAidApp* app = context;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

// ============================================================================
// T9-MINUS SCREEN - NAVIGATION
// ============================================================================

static void t9_move_cursor(int8_t line_delta, int8_t pos_delta) {
    if(line_delta != 0) {
        int8_t new_line = t9_cursor.line + line_delta;
        if(new_line >= 0 && new_line < 4) {
            t9_cursor.line = new_line;
            // Clamp position to new line length
            int8_t min_pos = (t9_cursor.line == 2 || t9_cursor.line == 3) ? -1 : 0;  // Line 2 and 3 have special buttons at -1
            int8_t max_pos = strlen(t9_lines[t9_cursor.line]) - 1;
            if(t9_cursor.pos < min_pos) {
                t9_cursor.pos = min_pos;
            } else if(t9_cursor.pos > max_pos) {
                t9_cursor.pos = max_pos;
            }
        }
    }
    
    if(pos_delta != 0) {
        int8_t new_pos = t9_cursor.pos + pos_delta;
        int8_t min_pos = (t9_cursor.line == 2 || t9_cursor.line == 3) ? -1 : 0;
        int8_t max_pos = strlen(t9_lines[t9_cursor.line]) - 1;
        if(new_pos >= min_pos && new_pos <= max_pos) {
            t9_cursor.pos = new_pos;
        }
    }
}

static void t9_add_character(TypeAidApp* app) {
    // Check if we're on the "shft" button (line 2, pos -1)
    if(t9_cursor.line == 2 && t9_cursor.pos == -1) {
        shift_locked = !shift_locked;
        FURI_LOG_I(TAG, "Shift lock toggled: %s", shift_locked ? "ON" : "OFF");
        return;
    }
	// Check if we're on the "[space]" button (line 3, pos -1)
    if(t9_cursor.line == 3 && t9_cursor.pos == -1) {
        size_t current_len = strlen(app->text_buffer);
        if(current_len < TEXT_BUFFER_SIZE - 1) {
            app->text_buffer[current_len] = ' ';
            app->text_buffer[current_len + 1] = '\0';
            FURI_LOG_I(TAG, "Added space, buffer now: '%s'", app->text_buffer);
        }
        return;
    }
	
    
    size_t current_len = strlen(app->text_buffer);
    if(current_len < TEXT_BUFFER_SIZE - 1) {
		const char* line_str = shift_locked ? t9_lines_upper[t9_cursor.line] : t9_lines[t9_cursor.line];
        char ch = line_str[t9_cursor.pos];
        app->text_buffer[current_len] = ch;
        app->text_buffer[current_len + 1] = '\0';
        FURI_LOG_I(TAG, "Added char '%c', buffer now: '%s'", ch, app->text_buffer);
    }
}

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
        canvas_draw_icon(canvas, 46, 1, &I_splash);
    }
    // Draw icon and title at the top
    canvas_draw_icon(canvas, 1, 1, &I_icon_10x10);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 12, 1, AlignLeft, AlignTop, "Type Aid v0.1");
    
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    
    
    // Display entered text inside the box (truncated, no scrolling)
    if(strlen(app->text_buffer) > 0) {
		const uint8_t box_x = -1;
		const uint8_t box_y = 16;
		const uint8_t box_width = 1;
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
        canvas_draw_str_aligned(canvas, 1, 17, AlignLeft, AlignTop, "Try different");
        canvas_draw_str_aligned(canvas, 1, 26, AlignLeft, AlignTop, "keyboards");
    }	
	

    // Draw navigation hints at bottom
    elements_button_center(canvas, "New");
	elements_button_right(canvas, "Dflt");
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
    
    FURI_LOG_D(TAG, "Creating viewport for T9");
    app->t9_view_port = view_port_alloc();
    view_port_draw_callback_set(app->t9_view_port, t9_draw_callback, app);
    view_port_input_callback_set(app->t9_view_port, t9_input_callback, app);
    
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
	
	t9plus_init(); // Initialize T9+ prediction system
	
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
    view_port_free(app->t9_view_port);
    
    view_dispatcher_remove_view(app->view_dispatcher, 1);
    text_input_free(app->text_input);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_message_queue_free(app->event_queue);
    furi_record_close(RECORD_GUI);
    free(app);
    
    t9plus_deinit(); // Clean up T9+ prediction system
	
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
    
    bool in_t9_mode = false;
    
    FURI_LOG_I(TAG, "Entering main event loop"); // --------------------------
    while(1) {
        if(app->in_text_input) {
            // We're waiting for text input to finish
            furi_delay_ms(100); 
        } else {
            // Handle events based on current mode
            if(furi_message_queue_get(app->event_queue, &event, 100) == FuriStatusOk) {
                
                if(in_t9_mode) {
                    // T9 mode event handling
                    if(event.type == InputTypeShort || event.type == InputTypeLong) {
                        if(event.key == InputKeyBack) {
                            FURI_LOG_I(TAG, "Back pressed in T9, returning to splash");
                            in_t9_mode = false;
                            t9_cursor.line = 0;
                            t9_cursor.pos = 0;
							shift_locked = false;  // Reset shift lock
                            gui_remove_view_port(app->gui, app->t9_view_port);
                            gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
                        } else if(event.key == InputKeyOk) {
                            t9_add_character(app);
                            view_port_update(app->t9_view_port);
                        } else if(event.key == InputKeyUp) {
                            t9_move_cursor(-1, 0);
                            view_port_update(app->t9_view_port);
                        } else if(event.key == InputKeyDown) {
                            t9_move_cursor(1, 0);
                            view_port_update(app->t9_view_port);
                        } else if(event.key == InputKeyLeft) {
                            t9_move_cursor(0, -1);
                            view_port_update(app->t9_view_port);
                        } else if(event.key == InputKeyRight) {
                            t9_move_cursor(0, 1);
                            view_port_update(app->t9_view_port);
                        }
                    }
                } else if(event.type == InputTypeShort || event.type == InputTypeLong) {
                    // Splash screen event handling
                    if(event.key == InputKeyBack) {
                        FURI_LOG_I(TAG, "Back pressed, exiting");
                        break;
                    }
                    else if(event.key == InputKeyOk) {
                        FURI_LOG_I(TAG, "OK pressed, showing T9 input");
                        in_t9_mode = true;
                        gui_remove_view_port(app->gui, app->view_port);
                        gui_add_view_port(app->gui, app->t9_view_port, GuiLayerFullscreen);
                    }
                    else if(event.key == InputKeyDown || event.key == InputKeyRight) {
                        FURI_LOG_I(TAG, "Down/Right pressed, showing text input");
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
            
            if(!in_t9_mode) {
                view_port_update(app->view_port);
            }
        }
    }
    FURI_LOG_I(TAG, "Cleaning up");
    type_aid_app_free(app);
    FURI_LOG_I(TAG, "App exiting");
    return 0;
}