/**
 * Server-side spell checker implementation
 * This file contains the core functionality for spell checking and finding similar words
 * using Levenshtein distance algorithm.
 */

#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <signal.h>

#define INPUT_CHARACTER_LIMIT 60
#define LEVENSHTEIN_LIST_LIMIT 5
#define MAX_WORD_LENGTH 50
#define MAX_WORDS 100
#define DICTIONARY_FILE "basic_english_2000.txt"

// Global variables
int socket_desc = -1;
int new_socket = -1;

// Function Prototypes
void trim_response(char *response);
int word_exists(char dictionary[][50], int dictionary_size, const char *word);
void find_closest_matches(char dictionary[][50], int dictionary_size, const char *word, char matches[LEVENSHTEIN_LIST_LIMIT][50], int *exact_match);
int levenshtein_distance(const char *s1, const char *s2);
void to_lowercase(char *str);
int compare_strings(const void *a, const void *b);

// Trim trailing spaces and newline characters from a response
void trim_response(char *response) {
    char *start = response;
    while (isspace((unsigned char)*start)) {
        start++;
    }

    char *end = response + strlen(response) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    memmove(response, start, strlen(start) + 1);
}

/**
 * Checks if a given word exists in the dictionary
 * 
 * @param dictionary: 2D array containing all valid words
 * @param dictionary_size: Number of words in the dictionary
 * @param word: Word to check for existence
 * @return: 1 if word exists, 0 if it doesn't
 */
int word_exists(char dictionary[][50], int dictionary_size, const char *word) {
    for (int i = 0; i < dictionary_size; i++) {
        if (strcmp(dictionary[i], word) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Helper function to find the minimum of three integers
 * Used in Levenshtein distance calculation
 * 
 * @param a: First number
 * @param b: Second number
 * @param c: Third number
 * @return: Smallest of the three numbers
 */
int min3(int a, int b, int c) {
    int min = a;
    if (b < min) min = b;
    if (c < min) min = c;
    return min;
}

/**
 * Calculates the Levenshtein (edit) distance between two strings
 * Edit distance is the minimum number of single-character edits required
 * to change one word into another
 * 
 * @param s1: First string
 * @param s2: Second string
 * @return: The Levenshtein distance between s1 and s2
 */
int levenshtein_distance(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    // longest word control
    if (len1 > MAX_WORD_LENGTH || len2 > MAX_WORD_LENGTH) {
        return -1;
    }

    // create matris
    int matrix[MAX_WORD_LENGTH + 1][MAX_WORD_LENGTH + 1];

    // Fill first row column
    for (int i = 0; i <= len1; i++) {
        matrix[i][0] = i;
    }
    for (int j = 0; j <= len2; j++) {
        matrix[0][j] = j;
    }

    // Fill the matris
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (tolower(s1[i-1]) == tolower(s2[j-1])) ? 0 : 1;
            
            int deletion = matrix[i-1][j] + 1;
            int insertion = matrix[i][j-1] + 1;
            int substitution = matrix[i-1][j-1] + cost;
            
            // Select the minimum value
            matrix[i][j] = deletion;
            if (insertion < matrix[i][j]) matrix[i][j] = insertion;
            if (substitution < matrix[i][j]) matrix[i][j] = substitution;
        }
    }

    return matrix[len1][len2];
}

// Convert a string to lowercase
void to_lowercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

// Compare two strings for qsort
int compare_strings(const void *a, const void *b) {
    return strcmp((char *)a, (char *)b);
}


struct WordMatch {
    char word[MAX_WORD_LENGTH];
    int distance;
    int length_diff;
};

int compare_matches(const void *a, const void *b) {
    const struct WordMatch *wa = (const struct WordMatch *)a;
    const struct WordMatch *wb = (const struct WordMatch *)b;
    

    if (wa->distance != wb->distance) {
        return wa->distance - wb->distance;
    }
    
    
    return strcmp(wa->word, wb->word);
}


void find_closest_matches(char dictionary[][50], int dictionary_size, const char *word, 
                         char matches[LEVENSHTEIN_LIST_LIMIT][50], int *exact_match) {
    struct WordMatch *all_matches = malloc(dictionary_size * sizeof(struct WordMatch));
    int match_count = 0;
    *exact_match = 0;

    // Calculate distances for all words
    for (int i = 0; i < dictionary_size; i++) {
        int distance = levenshtein_distance(word, dictionary[i]);
        
        // Check for exact match
        if (distance == 0) {
            *exact_match = 1;
        }
        
        // Add all words within reasonable distance (distance <= word length + 2)
        if (distance >= 0 && distance <= strlen(word) + 2) {
            strcpy(all_matches[match_count].word, dictionary[i]);
            all_matches[match_count].distance = distance;
            all_matches[match_count].length_diff = abs((int)strlen(dictionary[i]) - (int)strlen(word));
            match_count++;
        }
    }

    // Sort the results
    qsort(all_matches, match_count, sizeof(struct WordMatch), compare_matches);

    // Select the best LEVENSHTEIN_LIST_LIMIT matches
    int added = 0;
    
    // First add exact match if exists
    for (int i = 0; i < match_count && added < LEVENSHTEIN_LIST_LIMIT; i++) {
        if (all_matches[i].distance == 0) {
            strcpy(matches[added++], all_matches[i].word);
            break;
        }
    }

    // Then add words with closest distances
    for (int i = 0; i < match_count && added < LEVENSHTEIN_LIST_LIMIT; i++) {
        if (all_matches[i].distance > 0) {  // Skip distance 0 (already added)
            // Filter out words that are too long or too short
            int len_diff = abs((int)strlen(all_matches[i].word) - (int)strlen(word));
            if (len_diff <= 2) {  // Length difference should be at most 2 characters
                strcpy(matches[added++], all_matches[i].word);
            }
        }
    }

    // If not enough matches found, relax the length difference criteria
    if (added < LEVENSHTEIN_LIST_LIMIT) {
        for (int i = 0; i < match_count && added < LEVENSHTEIN_LIST_LIMIT; i++) {
            if (all_matches[i].distance > 0 && 
                strcmp(matches[added-1], all_matches[i].word) != 0) {
                strcpy(matches[added++], all_matches[i].word);
            }
        }
    }

    // Clear remaining slots
    for (int i = added; i < LEVENSHTEIN_LIST_LIMIT; i++) {
        matches[i][0] = '\0';
    }

    free(all_matches);
}

// Structure for thread synchronization
struct ThreadSync {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int current_index;
} thread_sync;

// Add mutex for dictionary updates
pthread_mutex_t dict_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to update dictionary
void update_dictionary(char dictionary[][50], int *dictionary_size, const char *word) {
    pthread_mutex_lock(&dict_mutex);
    
    // Update dictionary in memory
    strcpy(dictionary[*dictionary_size], word);
    (*dictionary_size)++;
    
    // Write to file
    FILE *file = fopen(DICTIONARY_FILE, "a");
    if (file != NULL) {
        fprintf(file, "%s\n", word);
        fclose(file);
    } else {
        printf("Error: Could not open dictionary file for writing.\n");
    }
    
    pthread_mutex_unlock(&dict_mutex);
}

// Function to load dictionary (to be called in main)
int load_dictionary(char dictionary[][50]) {
    FILE *file = fopen(DICTIONARY_FILE, "r");
    if (file == NULL) {
        printf("Warning: Could not open dictionary file. Creating new file.\n");
        file = fopen(DICTIONARY_FILE, "w");
        if (file == NULL) {
            printf("Error: Could not create dictionary file.\n");
            return 0;
        }
        fclose(file);
        return 0;
    }

    int dictionary_size = 0;
    char line[50];
    
    while (fgets(line, sizeof(line), file) && dictionary_size < 1000) {
        // Remove newline character
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) > 0) {
            strcpy(dictionary[dictionary_size], line);
            dictionary_size++;
        }
    }
    
    fclose(file);
    return dictionary_size;
}

// Extend thread data structure
struct ThreadData {
    char word[MAX_WORD_LENGTH];
    char matches[LEVENSHTEIN_LIST_LIMIT][50];
    char selected_replacement[MAX_WORD_LENGTH];
    char (*dictionary)[50];
    int original_index;
    int socket;
    struct ThreadSync *sync;
    int total_words;
    int *dictionary_size_ptr;  // New name
};

// Helper function for character validation
int is_valid_character(char c) {
    return (isalpha(c) || isspace(c));
}

// Function to validate input string
int validate_input(const char *input, char *error_msg) {
    int length = strlen(input);
    
    // Check input length limits
    if (length > INPUT_CHARACTER_LIMIT) {
        snprintf(error_msg, 200, 
                "Error: Input length (%d) exceeds maximum limit (%d characters).\n"
                "Please enter a shorter text.\n", 
                length, INPUT_CHARACTER_LIMIT);
        return 0;
    }
    
    // Check for empty string
    if (length == 0) {
        strcpy(error_msg, "Error: Empty input string.\n");
        return 0;
    }

    // Character validation
    for (int i = 0; i < length; i++) {
        if (!is_valid_character(input[i])) {
            snprintf(error_msg, 200, 
                    "Error: Invalid character '%c' at position %d. Only letters and spaces are allowed.\n",
                    input[i], i + 1);
            return 0;
        }
    }

    return 1;
}

// Signal handler function
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\nShutting down server...\n");
        if (new_socket >= 0) {
            shutdown(new_socket, SHUT_RDWR);
            close(new_socket);
        }
        if (socket_desc >= 0) {
            shutdown(socket_desc, SHUT_RDWR);
            close(socket_desc);
        }
        exit(0);
    }
}

// Update thread function
void *process_word_thread(void *arg) {
    struct ThreadData *data = (struct ThreadData *)arg;
    
    // Wait for its turn
    pthread_mutex_lock(&data->sync->mutex);
    while (data->original_index != data->sync->current_index) {
        pthread_cond_wait(&data->sync->condition, &data->sync->mutex);
    }
    pthread_mutex_unlock(&data->sync->mutex);
    
    // Process the word
    to_lowercase(data->word);
    trim_response(data->word);
    
    // Check if word exists in dictionary (thread-safe)
    pthread_mutex_lock(&dict_mutex);
    int exact_match = word_exists(data->dictionary, *(data->dictionary_size_ptr), data->word);
    
    // Find closest matches
    find_closest_matches(data->dictionary, *(data->dictionary_size_ptr), data->word, 
                        data->matches, &exact_match);
    pthread_mutex_unlock(&dict_mutex);
    
    // Print output (single time)
    char matches_message[400];
    snprintf(matches_message, sizeof(matches_message), 
             "\nWORD %02d: %s\nMATCHES: ", data->original_index + 1, data->word);
    
    for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT && data->matches[i][0] != '\0'; i++) {
        int distance = levenshtein_distance(data->word, data->matches[i]);
        char temp[50];
        snprintf(temp, sizeof(temp), "%s (%d)%s", 
                data->matches[i], distance, 
                (i < LEVENSHTEIN_LIST_LIMIT - 1 && data->matches[i+1][0] != '\0') ? ", " : "");
        strcat(matches_message, temp);
    }
    strcat(matches_message, "\n\n");
    write(data->socket, matches_message, strlen(matches_message));

    // Dictionary check and user interaction (single time)
    if (!exact_match) {
        char query_message[200];
        snprintf(query_message, sizeof(query_message),
                "WORD %s is not present in dictionary.\nDo you want to add this word to dictionary? (y/N): ",
                data->word);
        write(data->socket, query_message, strlen(query_message));

        char response[10];
        int response_size = recv(data->socket, response, sizeof(response) - 1, 0);
        response[response_size] = '\0';
        trim_response(response);

        if (response[0] == 'y' || response[0] == 'Y') {
            update_dictionary(data->dictionary, data->dictionary_size_ptr, data->word);
            strcpy(data->selected_replacement, data->word);
        } else {
            strcpy(data->selected_replacement, data->matches[0]);
        }
    } else {
        char exists_message[200];
        snprintf(exists_message, sizeof(exists_message), 
                "WORD %s is present in dictionary.\n", data->word);
        write(data->socket, exists_message, strlen(exists_message));
        strcpy(data->selected_replacement, data->word);
    }

    // Wake up next thread
    pthread_mutex_lock(&data->sync->mutex);
    data->sync->current_index++;
    pthread_cond_broadcast(&data->sync->condition);
    pthread_mutex_unlock(&data->sync->mutex);

    return NULL;
}

int main(int argc, char *argv[]) {
    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Open dictionary file with error handling
    FILE *file = fopen("basic_english_2000.txt", "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Cannot open dictionary file 'basic_english_2000.txt'\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Load dictionary into memory with error checking
    char dictionary[3000][50];
    int dictionary_size = 0;
    while (fgets(dictionary[dictionary_size], sizeof(dictionary[dictionary_size]), file)) {
        if (dictionary_size >= 3000) {
            fprintf(stderr, "Warning: Dictionary size limit (3000 words) reached\n");
            fprintf(stderr, "Additional words will be ignored\n");
            break;
        }
        dictionary[dictionary_size][strcspn(dictionary[dictionary_size], "\n")] = '\0';
        dictionary_size++;
    }
    fclose(file);

    // Sort the dictionary alphabetically
    qsort(dictionary, dictionary_size, sizeof(dictionary[0]), compare_strings);

    // Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        printf("Could not create socket");
        return 1;
    }

    // Enable SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("setsockopt failed");
        return 1;
    }

    // Configure socket settings
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(60000);

    // Bind socket
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("Bind failed: %s\n", strerror(errno));  // Show error message
        return 1;
    }

    // Start listening
    listen(socket_desc, 3);

    // Accept client connections
    struct sockaddr_in client;
    int c = sizeof(struct sockaddr_in);
    
    while(1) {
        new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
        if (new_socket < 0) {
            printf("Accept failed");
            continue;
        }

        printf("Client connected.\n");

        // Inner loop - for single client processing
        int client_active = 1;
        while(client_active) {
            write(new_socket, "\nHello, this is Text Analysis Server!\nPlease enter your input string:\n", 69);
            
            char client_message[2000];
            memset(client_message, 0, sizeof(client_message));
            int read_size = recv(new_socket, client_message, 2000, 0);
            
            if (read_size <= 0) {
                printf("Client disconnected.\n");
                break;
            }

            // Clean input string
            client_message[strcspn(client_message, "\n")] = 0;
            client_message[strcspn(client_message, "\r")] = 0;

            // Input validation
            char error_msg[200];
            if (!validate_input(client_message, error_msg)) {
                write(new_socket, error_msg, strlen(error_msg));
                write(new_socket, "\nPlease try again with valid input.\n", 34);
                continue;
            }

            // Store original input
            char original_input[2000];
            strcpy(original_input, client_message);

            // Check word count
            int word_count = 0;
            char *temp = strdup(client_message);
            char *word = strtok(temp, " ");
            while (word != NULL) {
                word_count++;
                if (word_count > MAX_WORDS) {
                    free(temp);
                    char limit_msg[200];
                    snprintf(limit_msg, sizeof(limit_msg),
                            "Error: Too many words. Maximum %d words allowed.\n", MAX_WORDS);
                    write(new_socket, limit_msg, strlen(limit_msg));
                    word_count = 0;  // Reset word count
                    break;
                }
                word = strtok(NULL, " ");
            }
            free(temp);

            // Continue loop if word count limit exceeded
            if (word_count == 0) {
                continue;
            }

            // Create thread structures
            struct ThreadData *thread_data = malloc(word_count * sizeof(struct ThreadData));
            pthread_t *threads = malloc(word_count * sizeof(pthread_t));

            if (!thread_data || !threads) {
                write(new_socket, "Error: Memory allocation failed.\n", 32);
                free(thread_data);
                free(threads);
                continue;
            }

            // Start threads
            word = strtok(client_message, " ");
            int i = 0;
            while (word != NULL && i < word_count) {
                strcpy(thread_data[i].word, word);
                thread_data[i].original_index = i;
                thread_data[i].dictionary = dictionary;
                thread_data[i].dictionary_size_ptr = &dictionary_size;
                thread_data[i].socket = new_socket;
                thread_data[i].sync = &thread_sync;
                thread_data[i].total_words = word_count;

                if (pthread_create(&threads[i], NULL, process_word_thread, &thread_data[i]) != 0) {
                    write(new_socket, "Error: Thread creation failed.\n", 30);
                    break;
                }
                i++;
                word = strtok(NULL, " ");
            }

            // Wait for threads
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }

            // Combine results
            char final_processed_message[2000] = "";
            for (int j = 0; j < i; j++) {
                strcat(final_processed_message, thread_data[j].selected_replacement);
                if (j < i - 1) {
                    strcat(final_processed_message, " ");
                }
            }

            // Send final message
            char final_message[4000];
            snprintf(final_message, sizeof(final_message),
                    "INPUT: %s\nOUTPUT: %s\nThank you for using Text Analysis Server!\n",
                    original_input, final_processed_message);
            write(new_socket, final_message, strlen(final_message));

            // Notify client about connection closure and close connection
            const char *goodbye_msg = "Connection will be closed.\n";
            write(new_socket, goodbye_msg, strlen(goodbye_msg));
            
            client_active = 0;  // Exit inner loop
        }

        // Close client connection
        close(new_socket);
        printf("Client connection closed.\n");

        // Properly shutdown server
        shutdown(socket_desc, SHUT_RDWR);  // Shutdown in both directions
        close(socket_desc);
        printf("Server shut down.\n");
        exit(0);  // Terminate program
    }

    return 0;
}