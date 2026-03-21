#define _GNU_SOURCE
/*
UniStreak V2 CLI Client

Клиент для работы с регистратором UniStreak ® ВНИИА им. Духова (2‑я ревизия)

Версия клиента:     1.0.0 (UniStreakV2CLI-1.0.0-windows-x64)
Версия библиотеки:  1.0.0 (UniStreakV2Lib-1.0.0-windows-x64)

Автор:              Гусейнов Э.Т. @Snoparic
Организация:        ИЯФ СО РАН им. Г.И. Будкера, Новосибирск
Лицензия:           MIT License © 2026

Торговые марки "UNISTREAK" (№888444) и "ЮНИСТРИК" (№888445) принадлежат
правообладателю ФГУП «ВНИИА им. Духова».
*/

// Макросы для формирования информации о сборке
#ifdef _WIN32
    #if defined(__MINGW32__) || defined(__MINGW64__)
        #define COMPILER "MinGW"
    #elif defined(_MSC_VER)
        #define COMPILER "MSVC"
    #else
        #define COMPILER "unknown_windows"
    #endif
#else
    #ifdef __clang__
        #define COMPILER "Clang"
    #elif defined(__GNUC__)
        #define COMPILER "GCC"
    #else
        #define COMPILER "unknown_*nix"
    #endif
#endif

#ifdef __x86_64__
    #define ARCH "x86_64"
#elif defined(__i386__)
    #define ARCH "x86"
#elif defined(__aarch64__)
    #define ARCH "ARM64"
#else
    #define ARCH "unknown"
#endif

#ifdef _WIN32
    #define OS "Windows"
#elif __linux__
    #define OS "Linux"
#else
    #define OS "unknown"
#endif

/* Строка сборки с датой и временем */
#define BUILD_STRING "" BUILD_DATE " " __TIME__ " (" OS ", " ARCH ", " COMPILER ")"

#define _CRT_SECURE_NO_WARNINGS

#include "unistreak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>

/* ------------------------------------------------------------------
Платформенно-зависимые определения
------------------------------------------------------------------ */
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#define access _access
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define sleep_milliseconds(milliseconds) Sleep(milliseconds)
#else
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <time.h>
#include <strings.h>
#define sleep_milliseconds(milliseconds) \
    do \
    { \
        struct timespec delay = {0, (milliseconds) * 1000000}; \
        nanosleep(&delay, NULL); \
    } while (0)
#endif

/* ------------------------------------------------------------------
Константы приложения
------------------------------------------------------------------ */
#define MAX_FILENAME_LENGTH 32
#define MAX_INPUT_LENGTH 256
#define MAX_COMMAND_LENGTH 64
#define DEFAULT_CONNECTION_TIMEOUT_MS 1000
#define DEFAULT_IMAGE_TIMEOUT_MS 2000
#define DEFAULT_IMAGE_BASENAME "image"
#define CONFIG_FILENAME "unistreak_cli.cfg"
#define MAX_BUFFER_SIZE (10 * 1024 * 1024)
#define MAX_CONSECUTIVE_ERRORS 3
#define MAX_FRAME_INDEX 10000

/* ------------------------------------------------------------------
Структура конфигурации клиента
------------------------------------------------------------------ */
typedef struct
{
    char image_basename[MAX_FILENAME_LENGTH + 1];
    int connection_timeout_ms;
    int image_timeout_ms;
    char last_ip_address[64];
} client_configuration;

/* ------------------------------------------------------------------
Глобальные переменные состояния
------------------------------------------------------------------ */
static client_configuration current_configuration = {
    .image_basename = DEFAULT_IMAGE_BASENAME,
    .connection_timeout_ms = DEFAULT_CONNECTION_TIMEOUT_MS,
    .image_timeout_ms = DEFAULT_IMAGE_TIMEOUT_MS,
    .last_ip_address = ""
};

static unistreak_handle* active_camera_handle = NULL;
static bool is_camera_connected = false;
static bool is_camera_initialized = false;
static volatile sig_atomic_t interrupt_received = 0;


/* ------------------------------------------------------------------
Обработчик сигнала прерывания
------------------------------------------------------------------ */

/**
Обработчик сигнала прерывания (Ctrl+C).
Устанавливает флаг interrupt_received в true для корректного завершения
цикла непрерывного приёма изображений.
@param signal_number Номер полученного сигнала
*/
static void signal_handler_interrupt
(
    int signal_number
)
{
    (void)signal_number;
    interrupt_received = 1;
}

/* ------------------------------------------------------------------
Вспомогательные функции ввода-вывода
------------------------------------------------------------------ */

/**
Читает строку ввода от пользователя с обработкой конца файла.
Функция читает строку из stdin, удаляет символ новой строки если присутствует.
При переполнении буфера очищает остаток строки из stdin.
@param buffer Буфер для сохранения ввода (указатель!)
@param buffer_size Максимальный размер буфера
@return true при успешном чтении, false при EOF или ошибке
*/
static bool read_user_input(char* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
        return false;

    if (fgets(buffer, (int)buffer_size, stdin) == NULL)
        return false;

    size_t length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n')
        buffer[length - 1] = '\0';

    return true;
}

/**
Читает целое число от пользователя с проверкой диапазона.
Функция парсит введённую строку как целое число и проверяет что оно находится
в указанном диапазоне. При ошибке парсинга выводит сообщение об ошибке.
@param prompt Текст приглашения для ввода
@param minimum_value Минимальное допустимое значение
@param maximum_value Максимальное допустимое значение
@param output_value Указатель для сохранения результата
@return true при успешном валидном вводе, false при ошибке
*/
static bool read_integer_input
(
    const char* prompt,
    int minimum_value,
    int maximum_value,
    int* output_value
)
{
    if (prompt == NULL || output_value == NULL)
    {
        return false;
    }

    char input_buffer[MAX_INPUT_LENGTH];
    printf("%s", prompt);

    if (!read_user_input(input_buffer, sizeof(input_buffer)))
    {
        return false;
    }

    /* Проверяем что введено только число */
    char* end_pointer = NULL;
    errno = 0;
    long parsed_value = strtol(input_buffer, &end_pointer, 10);

    if (errno != 0 || end_pointer == input_buffer || *end_pointer != '\0')
    {
        printf("Ошибка: введено некорректное число.\n");
        return false;
    }

    if (parsed_value < minimum_value || parsed_value > maximum_value)
    {
        printf("Ошибка: значение должно быть в диапазоне [%d, %d].\n",
            minimum_value, maximum_value);
        return false;
    }

    *output_value = (int)parsed_value;
    return true;
}

/**
Читает непустую строку от пользователя.
Функция циклически запрашивает ввод пока пользователь не введёт непустую строку.
@param prompt Текст приглашения для ввода
@param buffer Буфер для сохранения результата
@param buffer_size Максимальный размер буфера
@return true при успешном вводе непустой строки, false при EOF
*/
static bool read_non_empty_string
(
    const char* prompt,
    char* buffer,
    size_t buffer_size
)
{
    if (prompt == NULL || buffer == NULL || buffer_size == 0)
    {
        return false;
    }

    while (true)
    {
        printf("%s", prompt);

        if (!read_user_input(buffer, buffer_size))
        {
            return false;
        }

        /* Проверяем что строка не пустая */
        size_t length = strlen(buffer);
        if (length > 0)
        {
            return true;
        }

        printf("Ввод не может быть пустым. Повторите.\n");
    }
}

/**
Ожидает нажатие клавиши Enter для продолжения.
Используется для паузы между выводами в интерактивном режиме,
давая пользователю время ознакомиться с выводом перед продолжением.
*/
static void wait_for_enter
(
    void
)
{
    printf("\nНажмите Enter для продолжения...");
    (void)getchar();
}

/* ------------------------------------------------------------------
Функции работы с файловой системой
------------------------------------------------------------------ */

/**
Проверяет существование файла по указанному пути.
Использует платформенно-зависимую функцию access для проверки.
@param file_path Путь к файлу для проверки
@return true если файл существует, false иначе
*/
static bool file_exists
(
    const char* file_path
)
{
    if (file_path == NULL)
    {
        return false;
    }

#ifdef _WIN32
    return access(file_path, 0) == 0;
#else
    return access(file_path, F_OK) == 0;
#endif
}

/**
Генерирует уникальное имя файла с авто-инкрементом индекса.
Алгоритм работы:
- Если файл "basename.tiff" не существует — возвращает его
- Иначе перебирает "basename_1.tiff", "basename_2.tiff" и т.д.
- Максимальный индекс ограничен MAX_FRAME_INDEX для предотвращения бесконечного цикла
@param base_name Базовое имя файла без расширения
@param output_buffer Буфер для результата
@param buffer_size Размер буфера
@return Указатель на output_buffer с сформированным именем
*/
static char* generate_unique_filename
(
    const char* base_name,
    char* output_buffer,
    size_t buffer_size
)
{
    if (base_name == NULL || output_buffer == NULL || buffer_size == 0)
    {
        return NULL;
    }

    /* Проверяем базовое имя без индекса */
    snprintf(output_buffer, buffer_size, "%s.tiff", base_name);

    if (!file_exists(output_buffer))
    {
        return output_buffer;
    }

    /* Перебираем индексы начиная с 1 */
    for (int index = 1; index < MAX_FRAME_INDEX; index++)
    {
        snprintf(output_buffer, buffer_size, "%s_%d.tiff", base_name, index);

        if (!file_exists(output_buffer))
        {
            return output_buffer;
        }
    }

    /* Если не нашли свободное имя — возвращаем последнее */
    return output_buffer;
}

/**
Сохраняет конфигурацию в файл для постоянного хранения.
Конфигурация сохраняется в формате ключ=значение, по одной записи на строку.
При ошибке записи выводит предупреждение но не прерывает работу.
@return true при успехе, false при ошибке записи
*/
static bool save_configuration_to_file
(
    void
)
{
    FILE* configuration_file = fopen(CONFIG_FILENAME, "w");

    if (configuration_file == NULL)
    {
        printf("Предупреждение: не удалось сохранить конфигурацию в файл.\n");
        return false;
    }

    fprintf(configuration_file, "image_basename=%s\n", current_configuration.image_basename);
    fprintf(configuration_file, "connection_timeout_ms=%d\n", current_configuration.connection_timeout_ms);
    fprintf(configuration_file, "image_timeout_ms=%d\n", current_configuration.image_timeout_ms);
    fprintf(configuration_file, "last_ip_address=%s\n", current_configuration.last_ip_address);

    fclose(configuration_file);
    return true;
}

/**
Загружает конфигурацию из файла при старте приложения.
Читает файл конфигурации построчно, парсит ключ=значение.
При отсутствии файла или ошибке используются значения по умолчанию.
Некритичные ошибки (неверный диапазон) игнорируются с использованием дефолтов.
*/
static void load_configuration_from_file
(
    void
)
{
    FILE* configuration_file = fopen(CONFIG_FILENAME, "r");

    if (configuration_file == NULL)
    {
        /* Файл не существует — используем значения по умолчанию */
        return;
    }

    char line_buffer[MAX_INPUT_LENGTH];

    while (fgets(line_buffer, sizeof(line_buffer), configuration_file) != NULL)
    {
        /* Удаляем символ новой строки */
        size_t length = strlen(line_buffer);
        if (length > 0 && line_buffer[length - 1] == '\n')
        {
            line_buffer[length - 1] = '\0';
        }

        /* Пропускаем пустые строки и комментарии */
        if (line_buffer[0] == '\0' || line_buffer[0] == '#')
        {
            continue;
        }

        /* Парсим ключ=значение */
        char* equals_sign = strchr(line_buffer, '=');

        if (equals_sign == NULL)
        {
            continue;
        }

        *equals_sign = '\0';
        char* key = line_buffer;
        char* value = equals_sign + 1;

        if (strcmp(key, "image_basename") == 0)
        {
            strncpy(current_configuration.image_basename, value, MAX_FILENAME_LENGTH);
            current_configuration.image_basename[MAX_FILENAME_LENGTH] = '\0';
        }
        else if (strcmp(key, "connection_timeout_ms") == 0)
        {
            int timeout_value = atoi(value);
            if (timeout_value > 0 && timeout_value <= 60000)
            {
                current_configuration.connection_timeout_ms = timeout_value;
            }
        }
        else if (strcmp(key, "image_timeout_ms") == 0)
        {
            int timeout_value = atoi(value);
            if (timeout_value > 0 && timeout_value <= 60000)
            {
                current_configuration.image_timeout_ms = timeout_value;
            }
        }
        else if (strcmp(key, "last_ip_address") == 0)
        {
            strncpy(current_configuration.last_ip_address, value, sizeof(current_configuration.last_ip_address) - 1);
            current_configuration.last_ip_address[sizeof(current_configuration.last_ip_address) - 1] = '\0';
        }
    }

    fclose(configuration_file);
}

/* ------------------------------------------------------------------
Функции отображения меню и помощи
------------------------------------------------------------------ */

/**
Отображает описание клиента.
*/
static void display_about
(
    void
)
{
    printf(
        "╔══════════════════════════════════════════════════════════════════════════════╗\n"
        "║                    Клиент UniStreak ® V2 CLI Client                          ║\n"
        "╠══════════════════════════════════════════════════════════════════════════════╣\n"
        "║ Клиент для работы с регистратором UniStreak ® ВНИИА им. Духова (2-я ревизия) ║\n"
        "║                                                                              ║\n"
        "║ Версия клиента:    1.0.0                                                     ║\n"
        "║ Версия библиотеки: 1.0.0                                                     ║\n"
        "║                                                                      [1.0.0] ║\n"
        "╟──────────────────────────────────────────────────────────────────────────────╢\n"
        "║ Автор:       Гусейнов Э.Т. @Snoparic                                         ║\n"
        "║ Организация: ИЯФ СО РАН им. Г.И. Будкера, Новосибирск                        ║\n"
        "║ Лицензия:    MIT License © 2026                                              ║\n"
        "╠══════════════════════════════════════════════════════════════════════════════╣\n"
        "║ Правообладатель ТМ \"UNISTREAK\" (№888444) и \"ЮНИСТРИК\" (№888445):             ║\n"
        "║                      ФГУП «ВНИИА им. Духова»                                 ║\n"
        "╚══════════════════════════════════════════════════════════════════════════════╝\n"
		" Собрано: "BUILD_STRING""
    );

}


/**
Отображает главное меню команд клиента.
Выводит полный список доступных команд с кратким описанием каждой.
Команды сгруппированы по функциональности: подключение, параметры, регистрация, изображения.
*/
static void display_main_menu
(
    void
)
{
    printf("\n═══ Клиент UniStreak ® V2 CLI Client ═══\n"
        "Доступные команды:\n"
        "> ПОДКЛЮЧЕНИЕ:\n"
        "  connect <ip>           — подключиться к камере\n"
        "  disconnect             — отключиться от камеры\n"
        "  status                 — показать статус подключения\n"
        "> ПАРАМЕТРЫ:\n"
        "  init                   — инициализировать камеру (загрузить параметры)\n"
        "  refresh                — обновить параметры с сервера\n"
        "  list                   — вывести список всех параметров\n"
        "  info <param>           — показать информацию о параметре\n"
        "  get <param>            — прочитать значение параметра\n"
        "  set <param> <value>    — установить значение параметра\n"
        "  enum <param>           — показать значения enum-параметра\n"
        "> РЕГИСТРАЦИЯ:\n"
        "  start                  — запустить регистрацию изображения\n"
        "  stop                   — остановить регистрацию изображения\n"
        "  reset                  — сбросить блокировку однократного запуска\n"
        "> ИЗОБРАЖЕНИЯ:\n"
        "  capture                — получить и сохранить одно изображение\n"
        "  capture_continuous     — непрерывный приём (периодический режим)\n"
        "  cc                     — аналог для capture_continuous\n"
        "> НАСТРОЙКИ:\n"
        "  settings               — открыть меню настроек клиента\n"
        "  help                   — показать эту справку\n"
        "  about                  — информация о программе\n"
        "  quit / exit            — выйти из программы\n"
        "════════════════════════\n");
    
}

/**
Отображает меню настроек клиента.
Показывает текущие значения всех настраиваемых параметров:
- Базовое имя файла для изображений
- Таймаут подключения
- Таймаут получения изображения
- Последний использованный IP-адрес
*/
static void display_settings_menu
(
    void
)
{
    printf("\n=== Настройки клиента ===\n");
    printf("1. Базовое имя файла изображения: \"%s\"\n", current_configuration.image_basename);
    printf("2. Таймаут подключения: %d мс\n", current_configuration.connection_timeout_ms);
    printf("3. Таймаут получения изображения: %d мс\n", current_configuration.image_timeout_ms);
    printf("4. Последний IP-адрес: %s\n",
        current_configuration.last_ip_address[0] != '\0' ? current_configuration.last_ip_address : "(не задан)");
    printf("\nВыберите пункт для изменения (1-4) или 0 для выхода: ");
}

/**
Отображает информацию о параметре из кэша.
Выводит тип параметра, описание на английском языке и текущее значение.
Для enum-параметров дополнительно показывает строковое представление значения.
@param parameter_name Имя параметра для отображения
*/
static void display_parameter_info
(
    const char* parameter_name
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    const char* parameter_type = unistreak_get_param_type(active_camera_handle, parameter_name);
    const char* parameter_description = unistreak_get_param_description(active_camera_handle, parameter_name);

    printf("\nПараметр: %s\n", parameter_name);

    if (parameter_type != NULL)
    {
        printf("  Тип: %s\n", parameter_type);
    }
    else
    {
        printf("  Тип: (не определён)\n");
    }

    if (parameter_description != NULL)
    {
        printf("  Описание: %s\n", parameter_description);
    }

    /* Показываем текущее значение если возможно */
    if (parameter_type != NULL)
    {
        if (strcmp(parameter_type, "int") == 0 || strcmp(parameter_type, "bool") == 0 || strcmp(parameter_type, "enum") == 0)
        {
            int integer_value;
            if (unistreak_get_int(active_camera_handle, parameter_name, &integer_value))
            {
                printf("  Текущее значение: %d", integer_value);

                /* Для enum показываем строковое представление */
                if (strcmp(parameter_type, "enum") == 0)
                {
                    const char* enum_string = unistreak_get_enum_string(active_camera_handle, parameter_name, integer_value);
                    if (enum_string != NULL)
                    {
                        printf(" (%s)", enum_string);
                    }
                }
                printf("\n");
            }
        }
        else if (strcmp(parameter_type, "float") == 0)
        {
            float float_value;
            if (unistreak_get_float(active_camera_handle, parameter_name, &float_value))
            {
                printf("  Текущее значение: %f\n", float_value);
            }
        }
    }
}

/* ------------------------------------------------------------------
Обработчики команд
------------------------------------------------------------------ */

/**
Обработчик команды подключения к камере.
Пытается установить соединение с камерой по указанному IP-адресу.
При ошибке подключения предлагает пользователю повторить попытку.
Цикл продолжается пока пользователь не введёт 'n'.
Сохраняет последний успешный адрес в конфигурацию.
@param ip_address IP-адрес камеры для подключения
*/
static void handle_connect_command
(
    const char* ip_address
)
{
    if (ip_address == NULL || *ip_address == '\0')
    {
        printf("Использование: connect <ip_address>\n");
        return;
    }

    if (is_camera_connected)
    {
        printf("Предупреждение: уже подключено к камере. Сначала выполните disconnect.\n");
        return;
    }

    /* Цикл повторных попыток подключения */
    bool connection_successful = false;

    while (connection_successful == false)
    {
        printf("Подключение к регистратору %s (таймаут: %d мс)...\n",
            ip_address, current_configuration.connection_timeout_ms);

        active_camera_handle = unistreak_connect
        (
            ip_address,
            current_configuration.connection_timeout_ms
        );

        if (active_camera_handle == NULL)
        {
            printf("Критическая ошибка: не удалось выделить память или инициализировать сеть.\n");
            return;
        }

        unistreak_error connection_error = unistreak_get_last_error(active_camera_handle);

        if (connection_error.code == UNISTREAK_OK)
        {
            connection_successful = true;
            break;
        }

        printf("Ошибка подключения: %s (код системы: %d)\n",
            connection_error.message,
            connection_error.sys_error);

        printf("Повторить попытку подключения? (y/n): ");

        char retry_answer_buffer[MAX_INPUT_LENGTH];
        bool read_success = read_user_input(retry_answer_buffer, sizeof(retry_answer_buffer));

        /* Если EOF или ошибка чтения — выходим */
        if (read_success == false)
        {
            printf("\nВвод прерван.\n");
            unistreak_disconnect(active_camera_handle);
            active_camera_handle = NULL;
            return;
        }

        /* Проверяем ответ (первый символ, игнорируем регистр) */
        char first_character = retry_answer_buffer[0];
        bool should_retry = (first_character == 'y' || first_character == 'Y');

        if (should_retry == false)
        {
            /* Пользователь отказался — выходим из цикла */
            unistreak_disconnect(active_camera_handle);
            active_camera_handle = NULL;
            return;
        }

        /* Очищаем дескриптор перед следующей попыткой */
        unistreak_disconnect(active_camera_handle);
        active_camera_handle = NULL;

        /* Дополнительная очистка stdin для Linux */
#ifndef _WIN32
        tcflush(STDIN_FILENO, TCIFLUSH);
#endif
    }

    /* Подключение успешно */
    printf("Успешное подключение. Проведите инициализацию регистратора командой init для получения текущих параметров и работы с ними.\n");
    is_camera_connected = unistreak_is_connected(active_camera_handle);

    /* Сохраняем последний успешный адрес */
    strncpy
    (
        current_configuration.last_ip_address,
        ip_address,
        sizeof(current_configuration.last_ip_address) - 1
    );
    current_configuration.last_ip_address[sizeof(current_configuration.last_ip_address) - 1] = '\0';
    save_configuration_to_file();
}

/**
Обработчик команды отключения от камеры.
Корректно закрывает все соединения и освобождает ресурсы дескриптора.
Если камера не подключена — выводит информационное сообщение.
*/
static void handle_disconnect_command
(
    void
)
{
    if (!is_camera_connected || active_camera_handle == NULL)
    {
        printf("Нет активного подключения к камере.\n");
        return;
    }

    printf("Отключение от устройства...\n");
    unistreak_disconnect(active_camera_handle);
    active_camera_handle = NULL;
    is_camera_connected = false;
    printf("Отключение выполнено.\n");
}

/**
Обработчик команды отображения статуса.
Показывает текущее состояние подключения, статус дескриптора,
серийный номер и IP-адрес устройства если доступна инициализация.
*/
static void handle_status_command
(
    void
)
{
    printf("\nСтатус подключения: %s\n", is_camera_connected ? "ПОДКЛЮЧЕНО" : "НЕ ПОДКЛЮЧЕНО");

    if (is_camera_connected && active_camera_handle != NULL)
    {
        printf("  Статус инициализации: %s\n", is_camera_initialized ? "проведена" : "отсутствует");
        printf("  Состояние дескриптора: %s\n",
            unistreak_is_connected(active_camera_handle) ? "активен" : "неактивен");

        const char* device_serial = unistreak_get_device_sn(active_camera_handle);
        const char* device_ip = unistreak_get_device_ip(active_camera_handle);

        if (device_serial != NULL)
        {
            printf("  Серийный номер: %s\n", device_serial);
        }

        if (device_ip != NULL)
        {
            printf("  IP-адрес устройства: %s\n", device_ip);
        }
    }
}

/**
Обработчик команды инициализации камеры.
Отправляет команду init на сервер и кэширует все параметры устройства.
После успешной инициализации выводит серийный номер и IP-адрес устройства.
*/
static void handle_init_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    printf("Инициализация сервера (загрузка параметров)...\n");
    unistreak_init(active_camera_handle);

    unistreak_error init_error = unistreak_get_last_error(active_camera_handle);

    if (init_error.code != UNISTREAK_OK)
    {
        printf("Ошибка инициализации: %s\n", init_error.message);
        return;
    }

    printf("Инициализация прошла успешно. Список параметров получен и записан в кэш дескиптора.\n");
    is_camera_initialized = unistreak_is_initialized(active_camera_handle);
}

/**
Обработчик команды обновления параметров.
Отправляет команду init для получения актуальных значений параметров.
Используется для синхронизации с изменениями от других клиентов.
*/
static void handle_refresh_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    printf("Обновление параметров с сервера...\n");
    unistreak_refresh_params(active_camera_handle);

    unistreak_error refresh_error = unistreak_get_last_error(active_camera_handle);

    if (refresh_error.code != UNISTREAK_OK)
    {
        printf("Ошибка обновления: %s\n", refresh_error.message);
        return;
    }

    printf("Параметры обновлены.\n");
}

/**
Обработчик команды вывода списка параметров.
Выводит все доступные имена параметров через пробел.
Требует предварительной инициализации камеры.
*/
static void handle_list_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }
    if (!is_camera_initialized)
    {
        printf("Список параметров не загружен в кэш. Выполните команду init для инициализации регистратора.\n");
        return;
    }

    const char* parameter_list = unistreak_get_param_list(active_camera_handle);


    printf("\nДоступные параметры:\n");
    printf("%s\n", parameter_list);
}

/**
Обработчик команды получения информации о параметре.
Вызывает display_parameter_info для вывода детальной информации.
@param parameter_name Имя параметра
*/
static void handle_info_command
(
    const char* parameter_name
)
{
    if (parameter_name == NULL || *parameter_name == '\0')
    {
        printf("Использование: info <parameter_name>\n");
        return;
    }

    display_parameter_info(parameter_name);
}

/**
Обработчик команды чтения значения параметра.
Определяет тип параметра и вызывает соответствующую функцию получения значения.
Для enum-параметров дополнительно показывает строковое представление.
@param parameter_name Имя параметра
*/
static void handle_get_command
(
    const char* parameter_name
)
{
    if (parameter_name == NULL || *parameter_name == '\0')
    {
        printf("Использование: get <parameter_name>\n");
        return;
    }

    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    const char* parameter_type = unistreak_get_param_type(active_camera_handle, parameter_name);

    if (parameter_type == NULL)
    {
        printf("Параметр '%s' не найден в кэше.\n", parameter_name);
        return;
    }

    if (strcmp(parameter_type, "int") == 0 || strcmp(parameter_type, "bool") == 0 || strcmp(parameter_type, "enum") == 0)
    {
        int integer_value;
        if (unistreak_get_int(active_camera_handle, parameter_name, &integer_value))
        {
            printf("%s = %d", parameter_name, integer_value);

            if (strcmp(parameter_type, "enum") == 0)
            {
                const char* enum_string = unistreak_get_enum_string(active_camera_handle, parameter_name, integer_value);
                if (enum_string != NULL)
                {
                    printf(" (%s)", enum_string);
                }
            }
            printf("\n");
        }
        else
        {
            printf("Ошибка чтения параметра: %s\n", unistreak_strerror(active_camera_handle));
        }
    }
    else if (strcmp(parameter_type, "float") == 0)
    {
        float float_value;
        if (unistreak_get_float(active_camera_handle, parameter_name, &float_value))
        {
            printf("%s = %f\n", parameter_name, float_value);
        }
        else
        {
            printf("Ошибка чтения параметра: %s\n", unistreak_strerror(active_camera_handle));
        }
    }
    else if (strcmp(parameter_type, "action") == 0)
    {
        printf("Параметр '%s' является действием (action) и не имеет значения для чтения.\n", parameter_name);
    }
    else
    {
        printf("Неизвестный тип параметра: %s\n", parameter_type);
    }
}

/**
Обработчик команды установки значения параметра.
Определяет тип параметра и преобразует строковое значение в нужный формат.
Отправляет команду set_params на сервер и обновляет локальный кэш.
@param parameter_name Имя параметра
@param value_string Строковое представление значения
*/
static void handle_set_command
(
    const char* parameter_name,
    const char* value_string
)
{
    if (parameter_name == NULL || *parameter_name == '\0' || value_string == NULL)
    {
        printf("Использование: set <parameter_name> <value>\n");
        return;
    }

    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    const char* parameter_type = unistreak_get_param_type(active_camera_handle, parameter_name);

    if (parameter_type == NULL)
    {
        printf("Параметр '%s' не найден в кэше.\n", parameter_name);
        return;
    }

    unistreak_clear_error(active_camera_handle);

    if (strcmp(parameter_type, "int") == 0 || strcmp(parameter_type, "bool") == 0 || strcmp(parameter_type, "enum") == 0)
    {
        int integer_value = atoi(value_string);
        unistreak_set_int(active_camera_handle, parameter_name, integer_value);
    }
    else if (strcmp(parameter_type, "float") == 0)
    {
        float float_value = (float)atof(value_string);
        unistreak_set_float(active_camera_handle, parameter_name, float_value);
    }
    else if (strcmp(parameter_type, "action") == 0)
    {
        printf("Параметр '%s' является действием и не принимает значений.\n", parameter_name);
        return;
    }
    else
    {
        printf("Неизвестный тип параметра: %s\n", parameter_type);
        return;
    }

    unistreak_error set_error = unistreak_get_last_error(active_camera_handle);

    if (set_error.code != UNISTREAK_OK)
    {
        printf("Ошибка установки параметра: %s\n", set_error.message);
    }
    else
    {
        printf("Параметр '%s' установлен в значение '%s'.\n", parameter_name, value_string);
    }
}

/**
Обработчик команды отображения значений enum-параметра.
Перебирает возможные числовые значения (0-19) и выводит их строковые представления.
Полезно для понимания допустимых значений enum-параметров.
@param parameter_name Имя enum-параметра
*/
static void handle_enum_command
(
    const char* parameter_name
)
{
    if (parameter_name == NULL || *parameter_name == '\0')
    {
        printf("Использование: enum <parameter_name>\n");
        return;
    }

    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    const char* parameter_type = unistreak_get_param_type(active_camera_handle, parameter_name);

    if (parameter_type == NULL)
    {
        printf("Параметр '%s' не найден в кэше.\n", parameter_name);
        return;
    }

    if (strcmp(parameter_type, "enum") != 0)
    {
        printf("Параметр '%s' не является enum-типом (текущий тип: %s).\n", parameter_name, parameter_type);
        return;
    }

    printf("\nВозможные значения для параметра '%s':\n", parameter_name);

    /* Перебираем значения через get_enum_string для разных числовых значений */
    for (int enum_index = 0; enum_index < 20; enum_index++)
    {
        const char* enum_string = unistreak_get_enum_string(active_camera_handle, parameter_name, enum_index);

        if (enum_string != NULL)
        {
            printf("  %d -> %s\n", enum_index, enum_string);
        }
    }
}

/**
Обработчик команды запуска регистрации.
Отправляет команду start на сервер. После успешного выполнения
сервер начинает передачу данных изображений на порт 8190.
*/
static void handle_start_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    printf("Запуск регистрации изображения...\n");
    unistreak_start(active_camera_handle);

    unistreak_error start_error = unistreak_get_last_error(active_camera_handle);

    if (start_error.code != UNISTREAK_OK)
    {
        printf("Ошибка запуска: %s\n", start_error.message);
        return;
    }

    printf("Регистрация запущена. Используйте capture или capture_continuous для получения изображений.\n");
}

/**
Обработчик команды остановки регистрации.
Отправляет команду stop на сервер для прекращения передачи изображений.
В периодическом режиме сервер продолжит отправку до получения этой команды.
*/
static void handle_stop_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    printf("Остановка регистрации...\n");
    unistreak_stop(active_camera_handle);

    unistreak_error stop_error = unistreak_get_last_error(active_camera_handle);

    if (stop_error.code != UNISTREAK_OK)
    {
        printf("Предупреждение: ошибка остановки: %s\n", stop_error.message);
    }
    else
    {
        printf("Регистрация остановлена.\n");
    }
}

/**
Обработчик команды сброса блокировки.
Отправляет команду reset_lock на сервер. Используется после
однократного запуска (trigger_mode=single) для возможности повторного запуска.
Примечание: некоторые версии сервера могут не поддерживать эту команду.
*/
static void handle_reset_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    printf("Сброс блокировки однократного запуска...\n");
    unistreak_reset_lock(active_camera_handle);

    unistreak_error reset_error = unistreak_get_last_error(active_camera_handle);

    if (reset_error.code != UNISTREAK_OK)
    {
        printf("Предупреждение: ошибка сброса: %s\n", reset_error.message);
        printf("Примечание: некоторые версии сервера могут не поддерживать reset_lock.\n");
    }
    else
    {
        printf("Блокировка сброшена.\n");
    }
}

/**
Обработчик команды получения и сохранения одного изображения.
Выделяет буфер, вызывает unistreak_receive_image и сохраняет данные в файл.
Имя файла генерируется с авто-инкрементом индекса для уникальности.
*/
static void handle_capture_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    /* Выделяем буфер для изображения */
    uint8_t* image_buffer = (uint8_t*)malloc(MAX_BUFFER_SIZE);

    if (image_buffer == NULL)
    {
        printf("Ошибка: не удалось выделить память для изображения (%d байт).\n", MAX_BUFFER_SIZE);
        return;
    }

    printf("Получение изображения (таймаут: %d мс)...\n", current_configuration.image_timeout_ms);

    int image_size = unistreak_receive_image
    (
        active_camera_handle,
        image_buffer,
        MAX_BUFFER_SIZE,
        current_configuration.image_timeout_ms
    );

    if (image_size > 0)
    {
        printf("Получено изображение размером %d байт.\n", image_size);

        /* Генерируем уникальное имя файла */
        char output_filename[MAX_FILENAME_LENGTH + 16];
        generate_unique_filename(current_configuration.image_basename, output_filename, sizeof(output_filename));

        /* Сохраняем в файл */
        FILE* output_file = fopen(output_filename, "wb");

        if (output_file != NULL)
        {
            size_t bytes_written = fwrite(image_buffer, 1, (size_t)image_size, output_file);
            fclose(output_file);

            if (bytes_written == (size_t)image_size)
            {
                printf("Изображение сохранено в файл: %s\n", output_filename);
            }
            else
            {
                printf("Ошибка записи: записано %zu из %d байт.\n", bytes_written, image_size);
            }
        }
        else
        {
            printf("Ошибка: не удалось открыть файл '%s' для записи.\n", output_filename);
        }
    }
    else
    {
        unistreak_error capture_error = unistreak_get_last_error(active_camera_handle);
        printf("Ошибка получения изображения: %s\n",
            capture_error.code != UNISTREAK_OK ? capture_error.message : "Неизвестная ошибка");
    }

    free(image_buffer);
}

/**
Обработчик команды непрерывного получения изображений.
Работает в периодическом режиме: сохраняет изображения потоком до прерывания Ctrl+C.
Устанавливает обработчик сигнала SIGINT для корректного завершения.
При таймауте между кадрами продолжает ожидание (нормальное поведение).
Прерывается при множественных ошибках подряд или сигнале пользователя.
*/
static void handle_capture_continuous_command
(
    void
)
{
    if (active_camera_handle == NULL || !is_camera_connected)
    {
        printf("Ошибка: камера не подключена.\n");
        return;
    }

    /* Устанавливаем обработчик сигнала прерывания */
    interrupt_received = 0;
#ifdef _WIN32
    (void)signal(SIGINT, signal_handler_interrupt);
#else
    (void)signal(SIGINT, signal_handler_interrupt);
#endif

    printf("Непрерывный приём изображений (периодический режим).\n");
    printf("Нажмите Ctrl+C для остановки...\n");

    /* Выделяем буфер один раз */
    uint8_t* image_buffer = (uint8_t*)malloc(MAX_BUFFER_SIZE);
    if (image_buffer == NULL)
    {
        printf("Ошибка: не удалось выделить память для изображения.\n");
        return;
    }

    int frame_counter = 0;
    int error_counter = 0;

    /* Основной цикл приёма */
    while (interrupt_received == 0)
    {
        int image_size = unistreak_receive_image
        (
            active_camera_handle,
            image_buffer,
            MAX_BUFFER_SIZE,
            current_configuration.image_timeout_ms
        );

        if (image_size > 0)
        {
            /* Сбрасываем счётчик ошибок при успешном приёме */
            error_counter = 0;

            /* Генерируем имя файла с индексом */
            char output_filename[MAX_FILENAME_LENGTH + 32];
            snprintf
            (
                output_filename,
                sizeof(output_filename),
                "%s_%d.tiff",
                current_configuration.image_basename,
                frame_counter
            );

            /* Сохраняем изображение */
            FILE* output_file = fopen(output_filename, "wb");
            if (output_file != NULL)
            {
                size_t bytes_written = fwrite
                (
                    image_buffer,
                    1,
                    (size_t)image_size,
                    output_file
                );
                fclose(output_file);

                if (bytes_written == (size_t)image_size)
                {
                    printf("Кадр #%d сохранён: %s (%d байт)\n",
                        frame_counter, output_filename, image_size);
                    frame_counter++;
                }
                else
                {
                    printf("Предупреждение: записано %zu из %d байт в %s\n",
                        bytes_written, image_size, output_filename);
                }
            }
            else
            {
                printf("Ошибка: не удалось открыть файл '%s'\n", output_filename);
            }
        }
        else
        {
            /* Обработка ошибки приёма */
            unistreak_error capture_error = unistreak_get_last_error(active_camera_handle);

            /* Таймаут в периодическом режиме — это нормально (между кадрами) */
            if (capture_error.code == UNISTREAK_ERR_TIMEOUT)
            {
                /* Ждём немного и продолжаем */
                sleep_milliseconds(100);
                continue;
            }

            /* Другие ошибки — логируем и считаем */
            printf("Ошибка приёма кадра: %s\n", unistreak_strerror(active_camera_handle));
            error_counter++;

            /* Прерываем при множественных ошибках подряд */
            if (error_counter >= MAX_CONSECUTIVE_ERRORS)
            {
                printf("Слишком много ошибок подряд. Остановка.\n");
                break;
            }
        }
    }

    /* Освобождаем ресурсы */
    free(image_buffer);

    /* Восстанавливаем обработчик сигнала */
#ifdef _WIN32
    (void)signal(SIGINT, SIG_DFL);
#else
    (void)signal(SIGINT, SIG_DFL);
#endif

    printf("\nПриём завершён. Всего сохранено кадров: %d\nВведите команду stop для остановки процесса регистрации.", frame_counter);
}

/**
Обработчик меню настроек.
Предоставляет интерактивный интерфейс для изменения:
- Базового имени файла изображений
- Таймаута подключения
- Таймаута получения изображения
- Последнего IP-адреса (просмотр/очистка)
Все изменения сохраняются в файл конфигурации.
*/
static void handle_settings_command
(
    void
)
{
    while (true)
    {
        display_settings_menu();

        char choice_buffer[8];
        if (!read_user_input(choice_buffer, sizeof(choice_buffer)))
        {
            break;
        }

        int selected_option = atoi(choice_buffer);

        if (selected_option == 0)
        {
            /* Выход из меню настроек */
            break;
        }
        else if (selected_option == 1)
        {
            /* Изменение базового имени файла */
            char new_basename[MAX_FILENAME_LENGTH + 1];

            if (read_non_empty_string("Введите новое базовое имя файла (до 32 символов): ",
                new_basename, sizeof(new_basename)))
            {
                /* Проверяем длину */
                if (strlen(new_basename) > MAX_FILENAME_LENGTH)
                {
                    printf("Ошибка: имя файла слишком длинное (максимум %d символов).\n", MAX_FILENAME_LENGTH);
                }
                else
                {
                    /* Проверяем что имя содержит только допустимые символы */
                    bool valid_name = true;

                    for (size_t character_index = 0; character_index < strlen(new_basename); character_index++)
                    {
                        char current_character = new_basename[character_index];

                        if (!isalnum((unsigned char)current_character) && current_character != '_' && current_character != '-')
                        {
                            valid_name = false;
                            break;
                        }
                    }

                    if (valid_name)
                    {
                        strncpy(current_configuration.image_basename, new_basename, MAX_FILENAME_LENGTH);
                        current_configuration.image_basename[MAX_FILENAME_LENGTH] = '\0';
                        printf("Имя файла изменено на: %s\n", current_configuration.image_basename);
                        save_configuration_to_file();
                    }
                    else
                    {
                        printf("Ошибка: имя файла может содержать только буквы, цифры, '_' и '-'.\n");
                    }
                }
            }
        }
        else if (selected_option == 2)
        {
            /* Изменение таймаута подключения */
            int new_timeout;

            if (read_integer_input("Введите таймаут подключения в мс (100-60000): ",
                100, 60000, &new_timeout))
            {
                current_configuration.connection_timeout_ms = new_timeout;
                printf("Таймаут подключения установлен: %d мс.\n", new_timeout);
                save_configuration_to_file();
            }
        }
        else if (selected_option == 3)
        {
            /* Изменение таймаута получения изображения */
            int new_timeout;

            if (read_integer_input("Введите таймаут получения изображения в мс (100-60000): ",
                100, 60000, &new_timeout))
            {
                current_configuration.image_timeout_ms = new_timeout;
                printf("Таймаут изображения установлен: %d мс.\n", new_timeout);
                save_configuration_to_file();
            }
        }
        else if (selected_option == 4)
        {
            /* Просмотр/очистка последнего адреса */
            if (current_configuration.last_ip_address[0] != '\0')
            {
                printf("Последний IP-адрес: %s\n", current_configuration.last_ip_address);
                printf("Очистить? (y/n): ");
                char clear_answer[2] = { 0 };
                if (read_user_input(clear_answer, sizeof(clear_answer)) &&
                    (clear_answer[0] == 'y' || clear_answer[0] == 'Y'))
                {
                    current_configuration.last_ip_address[0] = '\0';
                    save_configuration_to_file();
                    printf("Адрес очищен.\n");
                }
            }
            else
            {
                printf("Последний адрес не задан.\n");
            }
        }
        else
        {
            printf("Неверный выбор. Введите число от 0 до 4.\n");
        }

        //wait_for_enter();
    }
}

/**
Парсит и выполняет команду из строки ввода.
Разбивает строку на токены, извлекает команду и аргументы.
Приводит команду к нижнему регистру для регистронезависимого сравнения.
Вызывает соответствующий обработчик команды.
@param command_line Строка с командой и аргументами
*/
static void execute_command
(
    const char* command_line
)
{
    if (command_line == NULL || *command_line == '\0')
    {
        return;
    }

    /* Копируем строку для токенизации */
    char command_buffer[MAX_INPUT_LENGTH];
    strncpy(command_buffer, command_line, sizeof(command_buffer) - 1);
    command_buffer[sizeof(command_buffer) - 1] = '\0';

    /* Извлекаем команду (первое слово) */
    char* command_token = strtok(command_buffer, " \t");

    if (command_token == NULL)
    {
        return;
    }

    /* Приводим к нижнему регистру для сравнения */
    for (size_t character_index = 0; command_token[character_index] != '\0'; character_index++)
    {
        command_token[character_index] = (char)tolower((unsigned char)command_token[character_index]);
    }

    /* Обработка команд */
    if (strcmp(command_token, "connect") == 0)
    {
        char* ip_address = strtok(NULL, " \t");
        handle_connect_command(ip_address);
    }
    else if (strcmp(command_token, "disconnect") == 0)
    {
        handle_disconnect_command();
    }
    else if (strcmp(command_token, "status") == 0)
    {
        handle_status_command();
    }
    else if (strcmp(command_token, "init") == 0)
    {
        handle_init_command();
    }
    else if (strcmp(command_token, "refresh") == 0)
    {
        handle_refresh_command();
    }
    else if (strcmp(command_token, "list") == 0)
    {
        handle_list_command();
    }
    else if (strcmp(command_token, "info") == 0)
    {
        char* parameter_name = strtok(NULL, " \t");
        handle_info_command(parameter_name);
    }
    else if (strcmp(command_token, "get") == 0)
    {
        char* parameter_name = strtok(NULL, " \t");
        handle_get_command(parameter_name);
    }
    else if (strcmp(command_token, "set") == 0)
    {
        char* parameter_name = strtok(NULL, " \t");
        char* value_string = strtok(NULL, " \t");
        handle_set_command(parameter_name, value_string);
    }
    else if (strcmp(command_token, "enum") == 0)
    {
        char* parameter_name = strtok(NULL, " \t");
        handle_enum_command(parameter_name);
    }
    else if (strcmp(command_token, "start") == 0)
    {
        handle_start_command();
    }
    else if (strcmp(command_token, "stop") == 0)
    {
        handle_stop_command();
    }
    else if (strcmp(command_token, "reset") == 0)
    {
        handle_reset_command();
    }
    else if (strcmp(command_token, "capture") == 0)
    {
        handle_capture_command();
    }
    else if (strcmp(command_token, "capture_continuous") == 0 || strcmp(command_token, "cc") == 0)
    {
        handle_capture_continuous_command();
    }
    else if (strcmp(command_token, "settings") == 0)
    {
        handle_settings_command();
    }
    else if (strcmp(command_token, "help") == 0 || strcmp(command_token, "?") == 0)
    {
        display_main_menu();
    }
    else if (strcmp(command_token, "about") == 0 || strcmp(command_token, "?") == 0)
    {
        display_about();
    }
    else if (strcmp(command_token, "quit") == 0 || strcmp(command_token, "exit") == 0)
    {
        /* Сигнал выхода — обрабатывается в main */
        return;
    }
    else
    {
        printf("Неизвестная команда: '%s'. Введите 'help' для списка команд.\n", command_token);
    }
}

/* ------------------------------------------------------------------
Точка входа в программу
------------------------------------------------------------------ */

/**
Основная функция программы.
Реализует интерактивный цикл обработки команд пользователя.
При запуске:
- Инициализирует локаль для корректного вывода UTF-8
- Загружает конфигурацию из файла
- Выводит справку (help)
- Подключается к камере если указан адрес в аргументах
- Запускает основной цикл обработки команд
- Корректно завершает подключение при выходе
@param argument_count Количество аргументов командной строки
@param argument_vector Массив аргументов командной строки
@return Код завершения программы (0 — успех)
*/
int main
(
    int argument_count,
    char* argument_vector[]
)
{
    /* Инициализация локали для корректного вывода */
    setlocale(LC_CTYPE, ".utf-8");
	
	#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	#endif

    printf("UniStreak CLI Client v1.0.0\n");
    printf("Гусейнов Э.Т. @Snoparic, ИЯФ СО РАН им. Будкера\n\n");

    /* Загружаем конфигурацию из файла */
    load_configuration_from_file();

    /* Выводим справку при старте */
    display_main_menu();

    /* Если указан адрес в аргументах — пытаемся подключиться сразу */
    if (argument_count > 1)
    {
        handle_connect_command(argument_vector[1]);
    }

    /* Основной интерактивный цикл */
    char input_buffer[MAX_INPUT_LENGTH];

    while (true)
    {
        printf("\n> ");
        fflush(stdout);

        if (!read_user_input(input_buffer, sizeof(input_buffer)))
        {
            printf("\nЗавершение работы.\n");
            break;
        }

        /* Проверяем команды выхода */
        if (strcasecmp(input_buffer, "quit") == 0 || strcasecmp(input_buffer, "exit") == 0)
        {
            printf("Завершение работы.\n");
            break;
        }

        /* Выполняем команду */
        execute_command(input_buffer);
    }

    /* Корректное завершение: отключаемся если подключены */
    if (is_camera_connected && active_camera_handle != NULL)
    {
        printf("Завершение подключения к камере...\n");
        unistreak_disconnect(active_camera_handle);
    }

    return 0;
}