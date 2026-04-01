/**
 * @file web_handler.h
 * @brief Embedded Web Server and REST API declarations.
 * @details Handles the user interface, Wi-Fi scanning, and persistent network configuration via LittleFS.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#pragma once

// ==============================================================================
// WEB SERVER HANDLER (Declarations)
// ==============================================================================

/**
 * @brief Mounts LittleFS, registers HTTP routes, and starts the web server.
 */
void web_server_init();

/**
 * @brief Polls incoming client HTTP connections.
 * @note Must be called frequently in the main loop.
 */
void web_server_task();