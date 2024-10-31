//
// Created by kandu on 29.10.2024.
//

#pragma once

#include <Arduino.h>
#include "pt.h"
#include "async_wait.h"


class pt_coroutine_base {
public:
    pt_coroutine_base() = default;

    virtual ~pt_coroutine_base() = default;

    void stop() {
        is_stopped = true;
    }

    void start() {
        lc = 0;
        is_stopped = false;
    }

    virtual THREAD_STATE run() = 0;

protected:
    size_t lc{};          ///< Локальная переменная для хранения текущей линии выполнения (используется switch-case).
    const char *name{};   ///< Имя протопотока (для отладки или идентификации).
    bool is_stopped{};    ///< Флаг, указывающий, остановлен ли протопоток.
    async_wait delay{0};
};

#ifdef PT_BEGIN
#undef PT_BEGIN
#define PT_BEGIN() bool PT_YIELD_FLAG = true; \
                     switch(lc) {         \
                     case 0:
#endif

#ifdef PT_STOP
#undef PT_STOP
#define PT_STOP() \
  do {              \
    lc = 0;   \
    is_stopped = true; \
    return PT_STOPPED;       \
  } while(0)
#endif

#ifdef PT_WAIT
#undef PT_WAIT
#define PT_WAIT(ms) \
  do {                  \
    delay(ms);          \
    PT_YIELD_FLAG = false;  \
    lc = __LINE__;    \
    case __LINE__:          \
    if ((!PT_YIELD_FLAG) || delay) { \
      return PT_YIELDED;    \
    }                       \
  } while(0)
#endif

#ifdef PT_END
#undef PT_END
#define PT_END() default: \
break; \
}         \
                   PT_YIELD_FLAG = false;  \
                   is_stopped = true;\
                   lc = 0;           \
                   return PT_ENDED;
#endif

//#ifdef PT_SCHEDULE_RESUME
//#undef PT_SCHEDULE_RESUME
//#define PT_SCHEDULE_RESUME() is_stopped = false;
//#endif
//
//#define PT_SCHEDULE_STOP() is_stopped = true;

class LedCoroutine : public pt_coroutine_base {
public:
    LedCoroutine() = default;

    THREAD_STATE run() final {
        PT_BEGIN();
                while (true) {
                    analogWrite(D5, led_state ? counter : 0xFF - counter);
                    PT_WAIT(led_transition_delay);

                    if (++counter >= 0xFF) {
                        counter = 0;
                        current_state = led_state;
                        PT_STOP();
                    }
                }
        PT_END();
    }

    void set_led(bool state, size_t transition_ms = 0) {
        if (led_state == state && !is_in_transition()) {
            return;  // Если уже в целевом состоянии и переход не нужен
        }
        led_state = state;

        if (transition_ms == 0) {  // Если переход не нужен
            counter = 0xFF;
            analogWrite(D5, state ? 0xFF : 0);
            stop();  // Остановка протопотока
        } else {
            led_transition_delay = transition_ms / 0xFF;
            counter = 0;
            start();  // Перезапуск протопотока для плавного перехода
        }
    }

private:
    bool is_in_transition() const {
        return counter < 0xFF;  // В переходном состоянии, если counter не достиг максимума
    }

    uint8_t counter = 0xFF;       ///< Счетчик для плавного изменения яркости.
    bool current_state = false;   ///< Текущее состояние светодиода.
    bool led_state = false;       ///< Целевое состояние светодиода.
    uint32_t led_transition_delay = 0; ///< Задержка для изменения яркости.
};
