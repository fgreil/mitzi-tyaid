#include "t9plus.h"
#include <furi.h>
#include <storage/storage.h>
#include <ctype.h>
#include <string.h>

#define TAG "T9Plus"

// Maximum words per tier
#define MAX_TIER_WORDS 1000
#define MAX_WORD_LEN 32

typedef struct {
    char** words;
    size_t count;
    size_t capacity;
} WordTier;

static struct {
    WordTier tier1;  // Function words
    WordTier tier2;  // Common lemmas
    WordTier tier3a; // Chat/internet slang
    WordTier tier3b; // Fillers
    WordTier tier4;  // Formal discourse
    bool initialized;
} t9plus_state = {0};

// Helper: Allocate word tier
static bool tier_alloc(WordTier* tier, size_t capacity) {
    tier->words = malloc(capacity * sizeof(char*));
    if(!tier->words) return false;
    tier->capacity = capacity;
    tier->count = 0;
    return true;
}

// Helper: Free word tier
static void tier_free(WordTier* tier) {
    if(tier->words) {
        for(size_t i = 0; i < tier->count; i++) {
            free(tier->words[i]);
        }
        free(tier->words);
        tier->words = NULL;
    }
    tier->count = 0;
    tier->capacity = 0;
}

// Helper: Add word to tier
static bool tier_add_word(WordTier* tier, const char* word) {
    if(tier->count >= tier->capacity) return false;
    
    tier->words[tier->count] = strdup(word);
    if(!tier->words[tier->count]) return false;
    
    tier->count++;
    return true;
}

// Helper: Load words from file
static bool load_tier_from_file(const char* path, WordTier* tier) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    
    bool success = false;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buffer[MAX_WORD_LEN + 2]; // +2 for newline and null terminator
        size_t bytes_read;
        size_t pos = 0;
        
        while(true) {
            bytes_read = storage_file_read(file, buffer + pos, 1);
            if(bytes_read == 0) {
                // End of file - process last word if exists
                if(pos > 0) {
                    buffer[pos] = '\0';
                    // Trim whitespace
                    while(pos > 0 && isspace((unsigned char)buffer[pos - 1])) {
                        buffer[--pos] = '\0';
                    }
                    if(pos > 0 && buffer[0] != '#') {
                        tier_add_word(tier, buffer);
                    }
                }
                break;
            }
            
            // Check for newline
            if(buffer[pos] == '\n' || buffer[pos] == '\r') {
                buffer[pos] = '\0';
                // Trim trailing whitespace
                while(pos > 0 && isspace((unsigned char)buffer[pos - 1])) {
                    buffer[--pos] = '\0';
                }
                
                // Add word if not empty and not a comment
                if(pos > 0 && buffer[0] != '#') {
                    tier_add_word(tier, buffer);
                }
                pos = 0;
            } else if(pos < MAX_WORD_LEN) {
                pos++;
            } else {
                // Word too long, skip to next line
                while(bytes_read > 0 && buffer[pos] != '\n' && buffer[pos] != '\r') {
                    bytes_read = storage_file_read(file, buffer + pos, 1);
                }
                pos = 0;
            }
        }
        
        success = true;
        storage_file_close(file);
    }
    
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    
    return success;
}

bool t9plus_init(void) {
    if(t9plus_state.initialized) {
        FURI_LOG_W(TAG, "Already initialized");
        return true;
    }
    
    FURI_LOG_I(TAG, "Initializing T9+ prediction system");
    
    // Allocate tiers
    if(!tier_alloc(&t9plus_state.tier1, MAX_TIER_WORDS)) {
        FURI_LOG_E(TAG, "Failed to allocate tier1");
        return false;
    }
    if(!tier_alloc(&t9plus_state.tier2, MAX_TIER_WORDS)) {
        FURI_LOG_E(TAG, "Failed to allocate tier2");
        tier_free(&t9plus_state.tier1);
        return false;
    }
    if(!tier_alloc(&t9plus_state.tier3a, MAX_TIER_WORDS)) {
        FURI_LOG_E(TAG, "Failed to allocate tier3a");
        tier_free(&t9plus_state.tier1);
        tier_free(&t9plus_state.tier2);
        return false;
    }
    if(!tier_alloc(&t9plus_state.tier3b, MAX_TIER_WORDS)) {
        FURI_LOG_E(TAG, "Failed to allocate tier3b");
        tier_free(&t9plus_state.tier1);
        tier_free(&t9plus_state.tier2);
        tier_free(&t9plus_state.tier3a);
        return false;
    }
    if(!tier_alloc(&t9plus_state.tier4, MAX_TIER_WORDS)) {
        FURI_LOG_E(TAG, "Failed to allocate tier4");
        tier_free(&t9plus_state.tier1);
        tier_free(&t9plus_state.tier2);
        tier_free(&t9plus_state.tier3a);
        tier_free(&t9plus_state.tier3b);
        return false;
    }
    
    // Load tier files
    bool all_loaded = true;
    all_loaded &= load_tier_from_file("/ext/apps_data/type_aid/data/tier1_function_words.txt", &t9plus_state.tier1);
    all_loaded &= load_tier_from_file("/ext/apps_data/type_aid/data/tier2_lemma_list.txt", &t9plus_state.tier2);
    all_loaded &= load_tier_from_file("/ext/apps_data/type_aid/data/tier3a_chat.txt", &t9plus_state.tier3a);
    all_loaded &= load_tier_from_file("/ext/apps_data/type_aid/data/tier3b_fillers.txt", &t9plus_state.tier3b);
    all_loaded &= load_tier_from_file("/ext/apps_data/type_aid/data/tier4_formal_discourse.txt", &t9plus_state.tier4);
    
    if(!all_loaded) {
        FURI_LOG_W(TAG, "Some tier files failed to load");
    }
    
    FURI_LOG_I(TAG, "Loaded words: tier1=%zu, tier2=%zu, tier3a=%zu, tier3b=%zu, tier4=%zu",
        t9plus_state.tier1.count,
        t9plus_state.tier2.count,
        t9plus_state.tier3a.count,
        t9plus_state.tier3b.count,
        t9plus_state.tier4.count);
    
    t9plus_state.initialized = true;
    return true;
}

void t9plus_deinit(void) {
    if(!t9plus_state.initialized) return;
    
    FURI_LOG_I(TAG, "Shutting down T9+");
    
    tier_free(&t9plus_state.tier1);
    tier_free(&t9plus_state.tier2);
    tier_free(&t9plus_state.tier3a);
    tier_free(&t9plus_state.tier3b);
    tier_free(&t9plus_state.tier4);
    
    t9plus_state.initialized = false;
}

bool t9plus_is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '\'';
}

// Helper: Case-insensitive prefix match
static bool starts_with_ci(const char* word, const char* prefix) {
    size_t prefix_len = strlen(prefix);
    if(strlen(word) < prefix_len) return false;
    
    for(size_t i = 0; i < prefix_len; i++) {
        if(tolower((unsigned char)word[i]) != tolower((unsigned char)prefix[i])) {
            return false;
        }
    }
    return true;
}

// Helper: Search tier for matches
static void search_tier(
    const WordTier* tier,
    const char* prefix,
    char suggestions[T9PLUS_MAX_SUGGESTIONS][T9PLUS_MAX_WORD_LENGTH],
    uint8_t* found_count,
    uint8_t max_suggestions
) {
    for(size_t i = 0; i < tier->count && *found_count < max_suggestions; i++) {
        if(starts_with_ci(tier->words[i], prefix)) {
            strncpy(suggestions[*found_count], tier->words[i], T9PLUS_MAX_WORD_LENGTH - 1);
            suggestions[*found_count][T9PLUS_MAX_WORD_LENGTH - 1] = '\0';
            (*found_count)++;
        }
    }
}

uint8_t t9plus_get_suggestions(
    const char* input,
    char suggestions[T9PLUS_MAX_SUGGESTIONS][T9PLUS_MAX_WORD_LENGTH],
    uint8_t max_suggestions
) {
    if(!t9plus_state.initialized) {
        FURI_LOG_W(TAG, "Not initialized");
        return 0;
    }
    
    if(!input || strlen(input) == 0) {
        return 0;
    }
    
    if(max_suggestions > T9PLUS_MAX_SUGGESTIONS) {
        max_suggestions = T9PLUS_MAX_SUGGESTIONS;
    }
    
    // Extract the last word from input
    const char* last_word = input;
    for(const char* p = input; *p; p++) {
        if(*p == ' ' || *p == '\n') {
            last_word = p + 1;
        }
    }
    
    // Skip if last word is empty or just whitespace
    if(strlen(last_word) == 0) {
        return 0;
    }
    
    uint8_t found = 0;
    
    // Search tiers in priority order: tier1, tier3a, tier3b, tier2, tier4
    search_tier(&t9plus_state.tier1, last_word, suggestions, &found, max_suggestions);
    if(found < max_suggestions) {
        search_tier(&t9plus_state.tier3a, last_word, suggestions, &found, max_suggestions);
    }
    if(found < max_suggestions) {
        search_tier(&t9plus_state.tier3b, last_word, suggestions, &found, max_suggestions);
    }
    if(found < max_suggestions) {
        search_tier(&t9plus_state.tier2, last_word, suggestions, &found, max_suggestions);
    }
    if(found < max_suggestions) {
        search_tier(&t9plus_state.tier4, last_word, suggestions, &found, max_suggestions);
    }
    
    return found;
}
