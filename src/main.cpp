#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "pt.h"
#include <Wire.h>

// ESP8266WebServer server(80);


class async_wait{
public:
    async_wait() = default;

    async_wait(size_t milliseconds){
        reload(milliseconds);
    }


    explicit operator bool() const{
        return !is_finished();
    }


    void operator()(size_t milliseconds){
        reload(milliseconds);
    }


    [[nodiscard]] bool is_finished() const{
        if(_ms==0)
            return true;
        return (_begin_time + _ms) <= millis();
    }


    void reload(size_t milliseconds = -1){
        if(milliseconds != -1)
            _ms = milliseconds;
        _begin_time = millis();
    }
private:
    size_t _ms = 0;
    size_t _begin_time = 0;
};



static pt task_1_context;
static pt task_2_context;


//#undef PT_THREAD
//#define PT_THREAD(name) char (name)(struct pt* pt, const char* task_name=#name)

static PT_THREAD(task_1){
    static async_wait delay(0);
    static size_t ctr = 0;
    PT_BEGIN(pt);
    while (true) {
        PT_WAIT_WHILE(pt, delay);
        delay(2500);
        printf("Counter[%s] is %d\n", task_name, ctr );
        ctr++;
        PT_YIELD(pt);
    }
    PT_END(pt);
}


static PT_THREAD(task_2) {
    static async_wait delay(0);
    static size_t ctr = 0;
    PT_BEGIN(pt);
    while (true) {
        PT_WAIT_WHILE(pt, delay);
        delay(1250);
        printf("Counter[%s] is %d\n", task_name, ctr);
        ctr++;
        PT_YIELD(pt);
    }
    PT_END(pt);
}

void cb(WiFiEvent_t event){
    switch (event) {
        case WIFI_EVENT_STAMODE_CONNECTED:
            Serial.println("WiFi подключен");
            break;
            //Если при подключении указан несуществующий SSID
            //Если пароль неправильный
        case WIFI_EVENT_STAMODE_DISCONNECTED:
            Serial.println("WiFi отключен");
            break;
            //Если SSID правильный
        case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
            Serial.println("Режим аутентификации изменен");
            break;
        case WIFI_EVENT_STAMODE_GOT_IP:
            Serial.print("Получен IP: ");
            Serial.println(WiFi.localIP());
            break;
        case WIFI_EVENT_STAMODE_DHCP_TIMEOUT:
            Serial.println("Ошибка DHCP");
            break;
        case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
            Serial.println("Клиент подключен к SoftAP");
            break;
        case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
            Serial.println("Клиент отключен от SoftAP");
            break;
        default:
            Serial.println("Неизвестное событие");
            break;
    }
}
WiFiEventHandler stationConnectedHandler;

WiFiEventHandler disconnectedEventHandler;
static pt nm_context;
#define MAX_WIFI_TRIES  5
static uint8_t tries_ctr = 0;

void wifi_disconnect_cb(const WiFiEventStationModeDisconnected& event){
    PT_SCHEDULE_RESUME(&nm_context);
    ++tries_ctr;
}

void wifi_connected_cb(const WiFiEventStationModeConnected& event){
    tries_ctr = 0;
}


struct bcd_t{
    size_t length;
    size_t pos;
    uint8_t* ptr;
};

//TODO: Собирать в конкретное число, а не байты
#include <algorithm>
size_t bcd_pop(bcd_t& bcd, size_t bit_length){
    size_t index = bcd.pos / 8;
    size_t bit_offset = bcd.pos % 8;
    size_t out = 0;
    if(bcd.pos + bit_length > bcd.length * 8)
        return -1;
    while(bit_length){
        const uint8_t val = *(bcd.ptr + index++);
        const size_t bits_extracted = std::min(8 - bit_offset, bit_length);
         out |= ((val >> (8 - bit_offset - bits_extracted)) & (0xFF >> (8 - bits_extracted)))
                 << (bit_length - bits_extracted);
        bit_length -= bits_extracted;
        bcd.pos += bits_extracted;
        bit_offset = 0;
    }
    return out;
}

int bit_count(uint32_t num) {
    int count = 0;
    while (num > 0) {
        count++;
        num >>= 1;
    }
    return count;
}
//TODO: Собирать в конкретное число, а не байты
//TODO: Добавление числа произвольного размера
//TODO: Проверки границ и размеров
//TODO: Отдкльно сохранять позицию вставкит  и чтения
bool bcd_push(bcd_t& bcd, size_t number) {
    size_t index = bcd.pos / 8;          // Индекс байта
    size_t bit_offset = bcd.pos % 8;     // Смещение внутри байта
    size_t digit_bit_length = 4;         // Каждая цифра занимает 4 бита

    do {
        uint8_t digit = number % 10;     // Извлекаем последнюю цифру
        number /= 10;

        // Вставляем цифру на нужное место с учетом текущей позиции
        uint8_t* val = bcd.ptr + index;

        // Если цифра полностью умещается в текущий байт
        if (bit_offset + digit_bit_length <= 8) {
            *val |= (digit << (8 - bit_offset - digit_bit_length));
            bcd.pos += digit_bit_length;
        } else {
            // Если цифра не умещается в текущий байт, делаем перенос
            size_t bits_in_current_byte = 8 - bit_offset;
            *val |= (digit >> (digit_bit_length - bits_in_current_byte));  // Ставим старшие биты

            // Переходим на следующий байт и вставляем оставшиеся биты
            val++;
            *val |= (digit << (8 - (digit_bit_length - bits_in_current_byte)));
            bcd.pos += digit_bit_length;
        }

        // Обновляем индекс и смещение
        index = bcd.pos / 8;
        bit_offset = bcd.pos % 8;

    } while (number > 0);

    return true;
}
byte decToBcd(byte val) {
    return (val / 10 * 16) + (val % 10);
}

byte bcdToDec(byte val) {
    return (val / 16 * 10) + (val % 16);
}
#define DS1307_ADDR 0x68
void setTime(byte hour, byte minute, byte second, byte day, byte month, byte year) {
    Wire.beginTransmission(DS1307_ADDR);
    Wire.write(0);  // Устанавливаем указатель на 0 регистр
    Wire.write(decToBcd(second));  // Секунды
    Wire.write(decToBcd(minute));  // Минуты
    Wire.write(decToBcd(hour));    // Часы
    Wire.write(1);                 // День недели (не используется)
    Wire.write(decToBcd(day));     // День
    Wire.write(decToBcd(month));   // Месяц
    Wire.write(decToBcd(year - 2000));  // Год (DS1307 хранит год как два последних символа)
    Wire.endTransmission();
}
void setup() {
    pinMode(D5, OUTPUT);
    Serial.begin(115200);
    pinMode(2, OUTPUT);
    digitalWrite(2, 1);
    PT_INIT(&nm_context);
    //PT_INIT(&task_2_context);
    //WiFi.setAutoReconnect(true);

    stationConnectedHandler = WiFi.onStationModeConnected(wifi_connected_cb);

    disconnectedEventHandler = WiFi.onStationModeDisconnected(wifi_disconnect_cb);

  //  WiFi.begin(SSID, PASSWORD);
    //WiFi.onEvent(cb);
//    if (WiFi.begin(SSID, PASSWORD) == WL_CONNECTED)
//        Serial.print("Wifi connected to AP");

   // Serial.print("qwerty");
    //WiFi.isConnected();
    //WiFi.setAutoReconnect();
    //WiFi.onStationModeDisconnected();
// write your initialization code here
    Wire.begin();

    setTime(12, 0, 0, 4, 10, 2024);
//    bcd_push(bcd, number);
//    Serial.print("BCD результат: ");
//
////    std::cout << "BCD результат: ";
//    for (int i = 0; i < 4; ++i) {
//        Serial.print(static_cast<int>(bcd_array[i]));
//        Serial.print(" ");
////        std::cout << std::hex << static_cast<int>(bcd_array[i]) << " ";
//    }
////    std::cout << std::endl;
//        bcd.pos=0;
//        size_t result1 = bcd_pop(bcd, 4);
//    Serial.println("BEGIN");
//    Serial.print("Result 1 (4 bits): ");
//    Serial.println(result1);
//
//    size_t result2 = bcd_pop(bcd, 4);
//
//    Serial.print("Result 2 (4 bits): ");
//    Serial.println(result2);
//    bcd_t bcd = { std::size(data), 0, data };
//    size_t result1 = bcd_pop(bcd, 4);
//    Serial.println("BEGIN");
//    Serial.print("Result 1 (4 bits): ");
//    Serial.println(result1, HEX);
//
//    size_t result2 = bcd_pop(bcd, 6);
//    Serial.print("Result 2 (6 bits): ");
//    Serial.println(result2, HEX);
//
//    size_t result3 = bcd_pop(bcd,5);
//    Serial.print("Result 3 (5 bits): ");
//    Serial.println(result3, HEX);
//
//    size_t result4 = bcd_pop(bcd, 8);
//    Serial.print("Result 4 (8 bits): ");
//    Serial.println(result4, HEX);
}

//static int protothread1_flag, protothread2_flag;
//
///**
// * The first protothread function. A protothread function must always
// * return an integer, but must never explicitly return - returning is
// * performed inside the protothread statements.
// *
// * The protothread function is driven by the main loop further down in
// * the code.
// */
//static int
//protothread1(struct pt *pt)
//{
//    /* A protothread function must begin with PT_BEGIN() which takes a
//       pointer to a struct pt. */
//    PT_BEGIN(pt);
//
//                /* We loop forever here. */
//                while(1) {
//                    /* Wait until the other protothread has set its flag. */
//                    PT_WAIT_UNTIL(pt, protothread2_flag != 0);
//                    printf("Protothread 1 running\n");
//
//                    /* We then reset the other protothread's flag, and set our own
//                       flag so that the other protothread can run. */
//                    protothread2_flag = 0;
//                    protothread1_flag = 1;
//
//                    /* And we loop. */
//                }
//
//                /* All protothread functions must end with PT_END() which takes a
//                   pointer to a struct pt. */
//    PT_END(pt);
//}
//
//
//static int protothread3(struct pt *pt){
//    //PT_BEGIN(pt);
//    {
//        char PT_YIELD_FLAG=1;
//        switch (pt->lc){
//            case 0:
//                while(true) {
//                    protothread2_flag = 1;
//                    //PT_WAIT_UNTIL(pt, protothread1_flag != 0);
//                    pt->lc = 64;
//                    case 64:
//                        if (!(protothread1_flag != 0)) {
//                            return 0;
//                        }
//                    printf("Protothread 2 running\n");
//                    protothread1_flag = 0;
//                }
//                //PT_END(pt);
//        };
//        PT_YIELD_FLAG = 0;
//        pt->lc = 0;
//        return 2;
//    }
//}
//
//
//
///**
// * The second protothread function. This is almost the same as the
// * first one.
// */
//static int
//protothread2(struct pt *pt)
//{
//    PT_BEGIN(pt);
//
//                while(1) {
//                    /* Let the other protothread run. */
//                    protothread2_flag = 1;
//
//                    /* Wait until the other protothread has set its flag. */
//                    PT_WAIT_UNTIL(pt, protothread1_flag != 0);
//                    printf("Protothread 2 running\n");
//
//                    /* We then reset the other protothread's flag. */
//                    protothread1_flag = 0;
//
//                    /* And we loop. */
//                    PT_YIELD(pt);
//                }
//    PT_END(pt);
//}
//
//#undef PT_WAITING
//#undef PT_EXITED
//#undef PT_ENDED
//#undef PT_YIELDED
//
//#undef PT_THREAD
//using state_t = uint32_t;
//enum PT_STATE: state_t {PT_WAITING,PT_EXITED, PT_ENDED, PT_YIELDED};
//
//#define PT_THREAD(name)   PT_STATE (name)(state_t* state)
//
//
//
//
//
//#define PT_BEGIN_N { char PT_YIELD_FLAG = 1; LC_RESUME(context)
//
//#define PT_END_N LC_END(context); PT_YIELD_FLAG = 0; \
//                   context = 0; state = PT_STATE::PT_ENDED; return; }
//
//#define PT_WAIT_UNTIL_N(condition)            \
//  do {                        \
//    LC_SET(context);                \
//    if(!(condition)) {                \
//      state = PT_WAITING; return;            \
//    }                        \
//  } while(0)
//
//#define PT_YIELD_N                \
//  do {                        \
//    PT_YIELD_FLAG = 0;                \
//    LC_SET(context);                \
//    if(PT_YIELD_FLAG == 0) {            \
//      state = PT_STATE::PT_YIELDED; return;            \
//    }                        \
//  } while(0)
//
//
//class c1_a{
//public:
//    virtual ~c1_a() = 0;
//    virtual  void run() = 0;
//};
//
//
//class c3{
//public:
//    PT_STATE run(){
//        PT_BEGIN_N;
//        while(state != PT_STATE::PT_ENDED && state != PT_STATE::PT_EXITED){
//            worker();
//            switch (state) {
//                case PT_WAITING:
//            }
//            PT_YIELD_N;
//        }
//        PT_END_N;
//    }
//    void stop(){
//        state = PT_STATE::PT_ENDED;
//    };
//protected:
//    state_t state = 0;
//    state_t context = 0;
//private:
//    virtual void worker() = 0;
//
//};
//
//
//class async_wait{
//public:
//    async_wait() = default;
//
//    async_wait(size_t milliseconds){
//        reload(milliseconds);
//    }
//
//
//    explicit operator bool() const{
//        return is_finished();
//    }
//
//
//    void operator()(size_t milliseconds){
//        reload(milliseconds);
//    }
//
//
//    [[nodiscard]] bool is_finished() const{
//        return (_begin_time + _ms) > millis();
//    }
//
//
//    void reload(size_t milliseconds = -1){
//        if(milliseconds != -1)
//            _ms = milliseconds;
//        _begin_time = millis();
//    }
//private:
//    size_t _ms{};
//    size_t _begin_time{};
//};
//
//
//
//class c2 : public c3{
//public:
//    void worker() final{
//        Serial.print("c2 task");
//        delay_timer(250);
//
//        //PT_WAIT_UNTIL_N(delay_timer);
//        //
//        if (!(delay_timer)) {
//            state = PT_WAITING;
//            return;
//        }
//        //
//
//    }
//private:
//    async_wait delay_timer;
//    size_t ctr = 0;
//};
//
//c3* task1 = new c2();



#define SSID "S21"
#define PASSWORD "rxzn8231"
static PT_THREAD(network_monitor) {
    static async_wait delay(0);
    PT_BEGIN(pt);

    while (true) {
        PT_YIELD_UNTIL(pt, !delay);

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("Connected[");
            Serial.print(SSID);
            Serial.print("]");
            PT_EXIT(pt);
        }

        if (tries_ctr >= MAX_WIFI_TRIES) {
            Serial.println("Too many tries. Timeout");
            tries_ctr = 0;
            WiFi.disconnect();
            delay(5 * 60 * 1000);
        }
        else {
            if (tries_ctr++ == 0)
                WiFi.begin(SSID, PASSWORD);
            Serial.print("Connecting to WIFI [");
            Serial.print(SSID);
            Serial.print("] #");
            Serial.println(tries_ctr);
            delay(1 * 1000);
        }
        PT_YIELD(pt);
    }

    PT_END(pt);
}



void loop() {
//    for (int brightness = 0; brightness <= 255; brightness++) {
//        analogWrite(D5, brightness);  // Устанавливаем ШИМ-сигнал
//        delay(10);  // Задержка для плавного изменения
//    }
//
//    // Плавное уменьшение яркости
//    for (int brightness = 255; brightness >= 0; brightness--) {
//        analogWrite(D5, brightness);  // Устанавливаем ШИМ-сигнал
//        delay(10);  // Задержка для плавного изменения
//    }
    Wire.beginTransmission(DS1307_ADDR);
    Wire.write(0);  // Начинаем с регистра 0
    Wire.endTransmission();
    Serial.println(Wire.requestFrom(DS1307_ADDR, 8));
    while(Wire.available()){
        Serial.print("Got DS byte: ");
        uint8_t ret = Wire.read();
        Serial.print("{ ");
        Serial.print((ret & 0xF0) >> 4);
        Serial.print(", ");
        Serial.print((ret & 0x0F));

        Serial.println("}");
    }

//    byte error, address;
//    int nDevices;
//
//    Serial.println("Scanning...");
//
//    nDevices = 0;
//    for(address = 1; address < 127; address++ )
//    {
//        // The i2c_scanner uses the return value of
//        // the Write.endTransmisstion to see if
//        // a device did acknowledge to the address.
//        Wire.beginTransmission(address);
//        error = Wire.endTransmission();
//
//        if (error == 0)
//        {
//            Serial.print("I2C device found at address 0x");
//            if (address<16)
//                Serial.print("0");
//            Serial.print(address,HEX);
//            Serial.println("  !");
//
//            nDevices++;
//        }
//        else if (error==4)
//        {
//            Serial.print("Unknown error at address 0x");
//            if (address<16)
//                Serial.print("0");
//            Serial.println(address,HEX);
//        }
//    }
//    if (nDevices == 0)
//        Serial.println("No I2C devices found\n");
//    else
//        Serial.println("done\n");

    delay(1000);           // wait 5 seconds for next scan
    //PT_SCHEDULE(network_monitor, nm_context);
    //task_1(&task_1_context);
    //task_2(&task_2_context);
}