#pragma once
#include <cstdint>
/**
 * @brief Структура для хранения состояния протопотока.
 */
struct pt {
    size_t lc{};          ///< Локальная переменная для хранения текущей линии выполнения (используется switch-case).
    const char *name{};   ///< Имя протопотока (для отладки или идентификации).
    bool is_stopped{};    ///< Флаг, указывающий, остановлен ли протопоток.
};

/**
 * @enum THREAD_STATE
 * @brief Перечисление возможных состояний протопотока.
 */
enum THREAD_STATE : uint8_t {
    PT_WAITING,  ///< Ожидание выполнения условия.
    PT_EXITED,   ///< Завершение выполнения протопотока.
    PT_ENDED,    ///< Протопоток завершил выполнение.
    PT_YIELDED,  ///< Протопоток был приостановлен (yield).
    PT_STOPPED   ///< Протопоток был остановлен.
};

/**
 * @brief Инициализация протопотока.
 * @param pt Указатель на структуру протопотока.
 */
#define PT_INIT(pt)   (pt)->lc = 0; \
                      (pt)->is_stopped = false;

/**
 * @brief Объявление функции протопотока.
 * @param name Имя функции протопотока.
 */
#define PT_THREAD(name) char name(struct pt* thread_context)

/**
 * @brief Начало протопотока. Создает локальный флаг PT_YIELD_FLAG и инициализирует switch-case.
 * @param pt Указатель на структуру протопотока.
 */
#define PT_BEGIN(pt) bool PT_YIELD_FLAG = true; \
                     switch((pt)->lc) {         \
                     case 0:

/**
 * @brief Конец протопотока. Завершает switch-case и обновляет состояние протопотока.
 * @param pt Указатель на структуру протопотока.
 */
#define PT_END(pt) default:break;}         \
                   PT_YIELD_FLAG = false;  \
                   (pt)->is_stopped = true;\
                   (pt)->lc = 0;           \
                   return PT_ENDED;

/**
 * @brief Ожидание выполнения условия. Протопоток приостанавливается до тех пор, пока условие не выполнится.
 * @param pt Указатель на структуру протопотока.
 * @param condition Условие для продолжения выполнения.
 */
#define PT_WAIT_UNTIL(pt, condition) \
  do {                               \
    (pt)->lc = __LINE__;             \
    case __LINE__:                   \
    if (!(condition)) {              \
      return PT_WAITING;             \
    }                                \
  } while(0)

/**
 * @brief Ожидание, пока условие не станет ложным.
 * @param pt Указатель на структуру протопотока.
 * @param cond Условие для проверки.
 */
#define PT_WAIT_WHILE(pt, cond)  PT_WAIT_UNTIL((pt), !(cond))

/**
 * @brief Ожидание завершения другого протопотока.
 * @param pt Указатель на структуру протопотока.
 * @param thread Протопоток для ожидания.
 */
#define PT_WAIT_THREAD(pt, thread) PT_WAIT_WHILE((pt), PT_SCHEDULE(thread))

/**
 * @brief Создание и выполнение дочернего протопотока.
 * @param pt Указатель на структуру протопотока.
 * @param child Дочерний протопоток.
 * @param thread Функция дочернего протопотока.
 */
#define PT_SPAWN(pt, child, thread) \
  do {                              \
    PT_INIT((child));               \
    PT_WAIT_THREAD((pt), (thread)); \
  } while(0)

/**
 * @brief Перезапуск протопотока.
 * @param pt Указатель на структуру протопотока.
 */
#define PT_RESTART(pt) \
  do {                 \
    PT_INIT(pt);       \
    return PT_WAITING; \
  } while(0)

/**
 * @brief Завершение выполнения протопотока.
 * @param pt Указатель на структуру протопотока.
 */
#define PT_EXIT(pt) \
  do {              \
    (pt)->lc = 0;   \
    return PT_EXITED; \
  } while(0)

/**
 * @brief Запуск протопотока, если он не остановлен.
 * @param name Имя протопотока.
 */
#define PT_SCHEDULE(name) if (!name##_context.is_stopped) { \
                            name(&name##_context);           \
                          }

/**
 * @brief Приостановка выполнения протопотока.
 * @param pt Указатель на структуру протопотока.
 */
#define PT_YIELD(pt) \
  do {               \
    PT_YIELD_FLAG = false; \
    (pt)->lc = __LINE__;   \
    case __LINE__:         \
    if (!PT_YIELD_FLAG) {  \
      return PT_YIELDED;   \
    }                      \
  } while(0)

/**
 * @brief Приостановка выполнения протопотока до выполнения условия.
 * @param pt Указатель на структуру протопотока.
 * @param cond Условие для продолжения выполнения.
 */
#define PT_YIELD_UNTIL(pt, cond) \
  do {                           \
    PT_YIELD_FLAG = false;       \
    (pt)->lc = __LINE__;         \
    case __LINE__:               \
    if ((!PT_YIELD_FLAG) || !(cond)) { \
      return PT_YIELDED;         \
    }                            \
  } while(0)

/**
 * @brief Приостановка выполнения протопотока до тех пор, пока условие не станет ложным.
 * @param pt Указатель на структуру протопотока.
 * @param cond Условие для проверки.
 */
#define PT_YIELD_WHILE(pt, cond) \
  do {                           \
    PT_YIELD_FLAG = false;       \
    (pt)->lc = __LINE__;         \
    case __LINE__:               \
    if ((!PT_YIELD_FLAG) || (cond)) { \
      return PT_YIELDED;         \
    }                            \
  } while(0)

/**
 * @brief Остановка выполнения протопотока.
 * @param pt Указатель на структуру протопотока.
 */
#define PT_STOP(pt) \
  do {              \
    (pt)->lc = 0;   \
    (pt)->is_stopped = true; \
    return PT_STOPPED;       \
  } while(0)

/**
 * @brief Ожидание указанного времени.
 * @param pt Указатель на структуру протопотока.
 * @param ms Время ожидания в миллисекундах.
 */
#define PT_WAIT(pt, ms) \
  do {                  \
    delay(ms);          \
    PT_YIELD_FLAG = false;  \
    (pt)->lc = __LINE__;    \
    case __LINE__:          \
    if ((!PT_YIELD_FLAG) || delay) { \
      return PT_YIELDED;    \
    }                       \
  } while(0)

/**
 * @brief Возобновление выполнения протопотока.
 * @param name Имя протопотока.
 */
#define PT_SCHEDULE_RESUME(name) name##_context.is_stopped = false;

/**
 * @brief Объявление протопотока с его контекстом.
 * @param name Имя протопотока.
 */
#define PT_THREAD_DECL(name) static pt name##_context{0, #name, false}; \
                             static char name(struct pt* thread_context);
