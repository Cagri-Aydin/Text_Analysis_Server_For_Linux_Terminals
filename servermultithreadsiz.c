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

#define INPUT_CHARACTER_LIMIT 100
#define LEVENSHTEIN_LIST_LIMIT 5
#define MAX_WORD_LENGTH 50

// Function Prototypes
void trim_response(char *response);
int word_exists(char dictionary[][50], int dictionary_size, const char *word);
void find_closest_matches(char dictionary[][50], int dictionary_size, const char *word, char matches[LEVENSHTEIN_LIST_LIMIT][50]);
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
    
    // Create a matrix to store distances
    static int matrix[MAX_WORD_LENGTH + 1][MAX_WORD_LENGTH + 1];
    
    // Check for word length limits
    if (len1 > MAX_WORD_LENGTH || len2 > MAX_WORD_LENGTH) {
        return -1;
    }

    // Initialize first row and column
    for (int i = 0; i <= len1; i++) {
        matrix[i][0] = i;
    }
    for (int j = 0; j <= len2; j++) {
        matrix[0][j] = j;
    }

    // Fill the matrix using dynamic programming
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            if (s1[i - 1] == s2[j - 1]) {
                // Characters match, no operation needed
                matrix[i][j] = matrix[i - 1][j - 1];
            } else {
                // Take minimum of delete, insert, or substitute operations
                matrix[i][j] = 1 + min3(
                    matrix[i - 1][j],     // deletion
                    matrix[i][j - 1],     // insertion
                    matrix[i - 1][j - 1]  // substitution
                );
            }
        }
    }

    // Return the bottom-right cell which contains the final distance
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

/**
 * Finds the closest matching words for a given input word using Levenshtein distance.
 * If the word exists in dictionary, it will be shown first in matches followed by similar words.
 * If the word doesn't exist, it will show the closest matches based on edit distance.
 * 
 * @param dictionary: 2D array containing all valid words
 * @param dictionary_size: Number of words in the dictionary
 * @param word: Input word to find matches for
 * @param matches: Array to store the resulting matches (up to LEVENSHTEIN_LIST_LIMIT matches)
 */
void find_closest_matches(char dictionary[][50], int dictionary_size, const char *word, char matches[LEVENSHTEIN_LIST_LIMIT][50]) {
    // Structure to store both word and its Levenshtein distance
    struct WordDistance {
        char word[50];
        int distance;
    } word_distances[dictionary_size];

    int match_count = 0;  // Keeps track of how many matches we've found
    int min_distance = 1000;  // Initialize with a large number to find minimum distance
    int exists = word_exists(dictionary, dictionary_size, word);  // Check if word exists in dictionary

    // If the word exists in dictionary, add it as the first match
    if (exists) {
        strcpy(matches[0], word);
        match_count = 1;
    }

    // Calculate Levenshtein distance for all words in dictionary
    for (int i = 0; i < dictionary_size; i++) {
        strcpy(word_distances[i].word, dictionary[i]);
        if (strcmp(dictionary[i], word) == 0) {
            continue;  // Skip the word itself as it's already in matches if it exists
        }
        word_distances[i].distance = levenshtein_distance(word, dictionary[i]);
        if (word_distances[i].distance < min_distance) {
            min_distance = word_distances[i].distance;
        }
    }

    // Add words with the minimum distance found
    for (int i = 0; i < dictionary_size && match_count < LEVENSHTEIN_LIST_LIMIT; i++) {
        if (word_distances[i].distance == min_distance && strcmp(word_distances[i].word, word) != 0) {
            strcpy(matches[match_count], word_distances[i].word);
            match_count++;
        }
    }

    // If we haven't filled all match slots, add words with the next closest distance
    if (match_count < LEVENSHTEIN_LIST_LIMIT) {
        int next_distance = min_distance + 1;
        for (int i = 0; i < dictionary_size && match_count < LEVENSHTEIN_LIST_LIMIT; i++) {
            if (word_distances[i].distance == next_distance && strcmp(word_distances[i].word, word) != 0) {
                strcpy(matches[match_count], word_distances[i].word);
                match_count++;
            }
        }
    }

    // Clear any remaining slots in the matches array
    for (int i = match_count; i < LEVENSHTEIN_LIST_LIMIT; i++) {
        matches[i][0] = '\0';
    }
}

int main(int argc, char *argv[]) {
    int socket_desc, new_socket, c;
    struct sockaddr_in server, client;
    char client_message[2000];
    int read_size;

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

    // Create socket with error handling
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        fprintf(stderr, "Error: Could not create socket\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Set socket options with error handling
    int opt = 1;
    if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "Error: Failed to set SO_REUSEADDR\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
        close(socket_desc);
        exit(EXIT_FAILURE);
    }

    // Configure server settings
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(60000);

    // Bind socket with error handling
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        fprintf(stderr, "Error: Binding failed\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
        close(socket_desc);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    listen(socket_desc, 3);

    // Accept connection with error handling
    c = sizeof(struct sockaddr_in);
    new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c);
    if (new_socket < 0) {
        fprintf(stderr, "Error: Accept failed\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
        close(socket_desc);
        exit(EXIT_FAILURE);
    }

    // Send welcome message
    char *welcome_message = "Hello, this is Text Analysis Server!\nPlease enter your input string:\n";
    if (write(new_socket, welcome_message, strlen(welcome_message)) < 0) {
        fprintf(stderr, "Error: Failed to send welcome message\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
    }

    // Receive client message with error handling
    memset(client_message, 0, sizeof(client_message));
    read_size = recv(new_socket, client_message, sizeof(client_message) - 1, 0);

    if (read_size <= 0) {
        fprintf(stderr, "Error: No input received or connection closed\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
        close(new_socket);
        close(socket_desc);
        exit(EXIT_FAILURE);
    }

    // Process client message
    client_message[read_size] = '\0';
    to_lowercase(client_message);

    char input_copy[2000];
    strcpy(input_copy, client_message);

    char *word = strtok(input_copy, " ");
    char processed_message[2000] = "";
    int word_count = 1;

    // Process each word
    while (word != NULL) {
        to_lowercase(word);
        trim_response(word);

        // Check word length
        if (strlen(word) >= MAX_WORD_LENGTH) {
            fprintf(stderr, "\nWarning: Word '%s' exceeds maximum length, truncating\n", word);
            word[MAX_WORD_LENGTH-1] = '\0';
        }

        char matches[LEVENSHTEIN_LIST_LIMIT][50];
        find_closest_matches(dictionary, dictionary_size, word, matches);

        // Send matches to client with error handling
        char matches_message[400];
        snprintf(matches_message, sizeof(matches_message), "WORD %02d: %s\nMATCHES: ", word_count, word);
        for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT && matches[i][0] != '\0'; i++) {
            int distance = levenshtein_distance(word, matches[i]);
            if (distance == -1) {
                fprintf(stderr, "\nWarning: Could not calculate distance for '%s'\n", matches[i]);
                continue;
            }
            char temp[50];
            snprintf(temp, sizeof(temp), "%s (%d), ", matches[i], distance);
            strcat(matches_message, temp);
        }
        strcat(matches_message, "\n");
        
        if (write(new_socket, matches_message, strlen(matches_message)) < 0) {
            fprintf(stderr, "Error: Failed to send matches message\n");
            fprintf(stderr, "Details: %s\n", strerror(errno));
        }

        // Handle dictionary operations
        if (word_exists(dictionary, dictionary_size, word)) {
            char exists_message[200];
            snprintf(exists_message, sizeof(exists_message), "\nWORD %02d: %s is already present in dictionary.\n\n", word_count, word);
            if (write(new_socket, exists_message, strlen(exists_message)) < 0) {
                fprintf(stderr, "\nError: Failed to send exists message\n");
            }
            strcat(processed_message, word);
        } else {
            // Handle adding new words to dictionary
            char query_message[200];
            snprintf(query_message, sizeof(query_message),
                     "\nWORD %s is not present in dictionary.\nDo you want to add this word to the dictionary? (y/N): ", word);
            if (write(new_socket, query_message, strlen(query_message)) < 0) {
                fprintf(stderr, "Error: Failed to send query message\n");
            }

            char response[100];
            int response_size;

            // Get user response with error handling
            while (1) {
                memset(response, 0, sizeof(response));
                response_size = recv(new_socket, response, sizeof(response) - 1, 0);

                if (response_size <= 0) {
                    fprintf(stderr, "Error: Connection closed or no response received\n");
                    char *error_message = "Error: Connection issues. Terminating connection.\n";
                    write(new_socket, error_message, strlen(error_message));
                    close(new_socket);
                    close(socket_desc);
                    exit(EXIT_FAILURE);
                }

                response[response_size] = '\0';
                trim_response(response);

                if (strcmp(response, "y") == 0 || strcmp(response, "Y") == 0 || 
                    strcmp(response, "n") == 0 || strcmp(response, "N") == 0) {
                    break;
                } else {
                    char *invalid_message = "Invalid input. Please enter 'y' or 'n':\n";
                    if (write(new_socket, invalid_message, strlen(invalid_message)) < 0) {
                        fprintf(stderr, "Error: Failed to send invalid input message\n");
                    }
                }
            }

            // Handle dictionary updates
            if (strcmp(response, "y") == 0 || strcmp(response, "Y") == 0) {
                if (dictionary_size >= 3000) {
                    fprintf(stderr, "Error: Cannot add word, dictionary is full\n");
                    char *full_message = "Cannot add word: dictionary is full\n";
                    write(new_socket, full_message, strlen(full_message));
                } else {
                    strcat(processed_message, word);
                    strcpy(dictionary[dictionary_size], word);
                    dictionary_size++;
                    qsort(dictionary, dictionary_size, sizeof(dictionary[0]), compare_strings);

                    // Update dictionary file
                    FILE *write_file = fopen("basic_english_2000.txt", "w");
                    if (write_file == NULL) {
                        fprintf(stderr, "Error: Cannot update dictionary file\n");
                        fprintf(stderr, "Details: %s\n", strerror(errno));
                    } else {
                        for (int i = 0; i < dictionary_size; i++) {
                            fprintf(write_file, "%s\n", dictionary[i]);
                        }
                        fclose(write_file);
                    }
                }
            } else {
                strcat(processed_message, matches[0]);
            }
        }

        strcat(processed_message, " ");
        word = strtok(NULL, " ");
        word_count++;
    }

    // Send final message
    processed_message[strlen(processed_message) - 1] = '\0';
    char final_message[4000];
    snprintf(final_message, sizeof(final_message),
             "\nINPUT: %s\nOUTPUT: %s\nThank you for using Text Analysis Server!\n",
             client_message, processed_message);
    
    if (write(new_socket, final_message, strlen(final_message)) < 0) {
        fprintf(stderr, "Error: Failed to send final message\n");
        fprintf(stderr, "Details: %s\n", strerror(errno));
    }

    close(new_socket);
    close(socket_desc);
    return 0;
}