// Copyright Indu Prakash

#include <TimeLib.h>

#define DATA_PIN 15 // 74595 pin#14
#define CLOCK_PIN 5 // 74595 pin#11
#define LATCH_PIN 4 // 74595 pin#12

#define NIXIE_A 16 // 74171 pin#3
#define NIXIE_B 12 // 74171 pin#4
#define NIXIE_C 13 // 74171 pin#6
#define NIXIE_D 14 // 74171 pin#7

#define NUMBER_OF_NIXIES 6
#define DATETIME_SWAP_DURATION 6000
#define IPADDRESS_DISPLAY_DURATION 1500
#define DEMO_DISPLAY_DURATION 400
#define COUNT_DISPLAY_DURATION 25
#define DELAY_BETWEEN_DIGITDISPLAY 2000

#define NIXIE_BINARY_POSITION(n) (1 << (NUMBER_OF_NIXIES - (n)-1))

#define NIXIE_MODE_NONE 0
#define NIXIE_MODE_CLOCK 1        //Clock
#define NIXIE_MODE_DEMO 2         //Demo (all digits going from 0-9)
#define NIXIE_MODE_COUNT 3        //Counter
#define NIXIE_MODE_IP 4           //Display the ip address
#define NIXIE_MODE_TIMELEFT 5     //Time left till midnight
#define NIXIE_MODE_STATIC 6       //Display static value passed through "disp" command
#define NIXIE_MODE_MAX 7

int _mode, _renderCount, _ipAddrPiece, _dateFormat;
int _timeValue[NUMBER_OF_NIXIES], _dateValue[NUMBER_OF_NIXIES], _buffer[NUMBER_OF_NIXIES];
unsigned long _renderTime, _renderWaitTime, _lastDraw;
unsigned long _lastTimeUpdateInstant, _lastDemoInstant, _displayDuration;
unsigned long _delayBetweenDigits;
bool _showTimeWhenConnected, _updating, _showingDate;

void _swapArray(int *source, int pos1, int pos2) {
    int temp = *(source + pos1);
    *(source + pos1) = *(source + pos2);
    *(source + pos2) = temp;
}

void _copyToBuffer(int *source) {
    for (int i = 0; i < NUMBER_OF_NIXIES; i++) {
        _buffer[i] = *(source + i);
    }
}

void _fillBuffer(int value) {
    for (int i = 0; i < NUMBER_OF_NIXIES; i++) {
        _buffer[i] = value;
    }
}

void _initTimeLeftMode() {
    _lastTimeUpdateInstant = 0;
    _mode = NIXIE_MODE_TIMELEFT;
}

void _updateTimeLeft(unsigned long current_time) {
    if ((current_time - _lastTimeUpdateInstant) >= 1000) { // Every second
        _lastTimeUpdateInstant = current_time;

        time_t t = now();
        int hr = hour(t);
        int min = minute(t);
        int sec = second(t);

        int hrDiff = 24 - hr - 1;
        int minDiff = (60 - min) - 1;
        int secDiff = (60 - sec);
        if (minDiff >= 60) {
            minDiff = minDiff - 60;
            hrDiff++;
        }

        if (secDiff >= 60) {
            secDiff = secDiff - 60;
            minDiff++;

            if (minDiff >= 60) {
                hrDiff++;
                minDiff = minDiff - 60;
            }
        }

        _timeValue[0] = hrDiff / 10;
        _timeValue[1] = hrDiff % 10;
        _timeValue[2] = minDiff / 10;
        _timeValue[3] = minDiff % 10;
        if (NUMBER_OF_NIXIES == 6) {
            _timeValue[4] = secDiff / 10;
            _timeValue[5] = secDiff % 10;
        }
    }
}

void _initClockMode() {
    _lastTimeUpdateInstant = 0;
    _displayDuration = 0;
    _showingDate = false;
    _mode = NIXIE_MODE_CLOCK;
}

void _updateDateTime(unsigned long current_time) {
    if ((current_time - _lastTimeUpdateInstant) >= 1000) { // Every second
        _lastTimeUpdateInstant = current_time;

        time_t t = now();
        int piece = hour(t);
        _timeValue[0] = piece / 10;
        _timeValue[1] = piece % 10;
        piece = minute(t);
        _timeValue[2] = piece / 10;
        _timeValue[3] = piece % 10;
        if (NUMBER_OF_NIXIES == 6) {
            piece = second(t);
            _timeValue[4] = piece / 10;
            _timeValue[5] = piece % 10;
        }

        piece = month(t);
        _dateValue[0] = piece / 10;
        _dateValue[1] = piece % 10;
        piece = day(t);
        _dateValue[2] = piece / 10;
        _dateValue[3] = piece % 10;

        if (_dateFormat == DATEFORMAT_DDMMYY) {
            _swapArray(_dateValue, 0, 2);
            _swapArray(_dateValue, 1, 3);
        }

        if (NUMBER_OF_NIXIES == 6) {
            piece = year(t) % 100;
            _dateValue[4] = piece / 10;
            _dateValue[5] = piece % 10;
        }
        // DEBUG_MSG_P(PSTR("%d%d:%d%d:%d%d\n"), _timeValue[0], _timeValue[1], _timeValue[2], _timeValue[3],
        // _timeValue[4],  _timeValue[5]);
    }
}

void _initDemoMode() {
    _lastDemoInstant = 0;
    _buffer[0] = -1;
    _mode = NIXIE_MODE_DEMO;
}

// Updates demo content in buffer. If in startup demo mode, then switched to clock mode upon Wifi connection.
void _updateDemoFrame(unsigned long current_time) {
    if ((current_time - _lastDemoInstant) >= DEMO_DISPLAY_DURATION) {
        if (_showTimeWhenConnected && (WiFi.status() == WL_CONNECTED)) {
            _initClockMode();
            _showTimeWhenConnected = false;
            return;
        }

        _lastDemoInstant = current_time;
        _fillBuffer((_buffer[0] + 1) % 10);
    }
}

void _initIPMode() {
    _lastDemoInstant = 0;
    _ipAddrPiece = 0;
    _mode = NIXIE_MODE_IP;
}

// Updates IP address in buffer.
void _updateIpFrame(unsigned long current_time) {
    if ((current_time - _lastDemoInstant) >= IPADDRESS_DISPLAY_DURATION) {
        _lastDemoInstant = current_time;
        IPAddress ip = WiFi.localIP();

        _fillBuffer(-1); // Turn all digits off

        int value = ip[_ipAddrPiece];               // e.g.   192
        _buffer[NUMBER_OF_NIXIES - 1] = value % 10; // 2
        value = value / 10;                         // 19
        if (value > 0) {
            _buffer[NUMBER_OF_NIXIES - 2] = value % 10; // 9
            value = value / 10;                         // 1
            if (value > 0) {
                _buffer[NUMBER_OF_NIXIES - 3] = value; // 1
            }
        }

        _ipAddrPiece = (_ipAddrPiece + 1) % 4;
    }
}

void _initCountMode() {
    _lastDemoInstant = 0;
    _buffer[NUMBER_OF_NIXIES - 1] = 0;
    _fillBuffer(-1);
    _mode = NIXIE_MODE_COUNT;
}

// Updates counter in buffer.
void _updateCountFrame(unsigned long current_time) {
    if ((current_time - _lastDemoInstant) >= COUNT_DISPLAY_DURATION) {
        _lastDemoInstant = current_time;

        int i = NUMBER_OF_NIXIES - 1;
        _buffer[i]++; // Increment right most, adjust rest
        for (; i > -1; i--) {
            if (_buffer[i] == 10) {
                _buffer[i] = 0;

                if (i > 0) {
                    if (_buffer[i - 1] == -1) {
                        _buffer[i - 1] = 1;
                    } else {
                        _buffer[i - 1]++;
                    }
                }
            }
        }
    }
}

void _printStatus() {
    switch (_mode) {
    case NIXIE_MODE_NONE:
        DEBUG_MSG_P(PSTR("none"));
        break;
    case NIXIE_MODE_DEMO:
        DEBUG_MSG_P(PSTR("demo"));
        break;
    case NIXIE_MODE_CLOCK:
        DEBUG_MSG_P(PSTR("clock"));
        break;
    case NIXIE_MODE_COUNT:
        DEBUG_MSG_P(PSTR("count"));
        break;
    case NIXIE_MODE_IP:
        DEBUG_MSG_P(PSTR("ip"));
        break;
    case NIXIE_MODE_TIMELEFT:
        DEBUG_MSG_P(PSTR("timeleft"));
        break;
    case NIXIE_MODE_STATIC:
        DEBUG_MSG_P(PSTR("static"));
        break;
    }
    DEBUG_MSG_P(PSTR(" renderCycles countBetween=%d wait=%d timeTaken=%d\n"), _renderCount, _renderWaitTime,
                _renderTime);
}

void _writeValue(int value) {
    unsigned int D = 0;
    unsigned int C = 0;
    unsigned int B = 0;
    unsigned int A = 0;

    switch (value) {
    case 1:
        A = 1;
    case 0:
        break;

    case 3:
        A = 1;
    case 2:
        B = 1;
        break;

    case 5:
        A = 1;
    case 4:
        C = 1;
        break;

    case 7:
        A = 1;
    case 6:
        C = B = 1;
        break;

    case 9:
        A = 1;
    case 8:
        D = 1;
        break;

    default:
        A = B = C = D = 1;
        break;
    }

    digitalWrite(NIXIE_A, A);
    digitalWrite(NIXIE_B, B);
    digitalWrite(NIXIE_C, C);
    digitalWrite(NIXIE_D, D);
}

void _turnNixieOn(int which) {
    digitalWrite(LATCH_PIN, 0);
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, which);
    digitalWrite(LATCH_PIN, 1);
}

void _turnAllNixiesOff() { _turnNixieOn(0); }
void _turnAllNixiesOn() { _turnNixieOn(255); }

// Button click callback
void nixieSwitchMode() {
    _mode++;
    if (_mode >= NIXIE_MODE_MAX) {
        _mode = NIXIE_MODE_CLOCK;
    }

    // skip NIXIE_MODE_STATIC for button press
    if (_mode == NIXIE_MODE_STATIC) {
        _mode++;
        if (_mode >= NIXIE_MODE_MAX) {
            _mode = NIXIE_MODE_CLOCK;
        }
    }

    _setMode(_mode);
    //_printStatus();
}

void _setMode(int mode) {
    switch (mode) {
    case NIXIE_MODE_CLOCK:
        _initClockMode();
        break;
    case NIXIE_MODE_DEMO:
        _initDemoMode();
        break;
    case NIXIE_MODE_COUNT:
        _initCountMode();
        break;
    case NIXIE_MODE_IP:
        _initIPMode();
        break;
    case NIXIE_MODE_TIMELEFT:
        _initTimeLeftMode();
        break;
    case NIXIE_MODE_STATIC:
        _mode = NIXIE_MODE_STATIC;
        break;
    }

    if (mode > NIXIE_MODE_NONE && mode < NIXIE_MODE_MAX) {
        // Don't switch to clock mode if command was issued
        _showTimeWhenConnected = false;
    }
}

void _display(int value) {
    for (int i = 1; i <= NUMBER_OF_NIXIES; i++) {
        _buffer[NUMBER_OF_NIXIES - i] = value % 10;
        value = value / 10;
    }
}

#if TERMINAL_SUPPORT

void _nixieTerminal() {
    terminalRegisterCommand(F("status"), [](Embedis *e) {
        _printStatus();
        terminalOK();
    });

    terminalRegisterCommand(F("delay"), [](Embedis *e) {
        if (e->argc > 1) {
            _delayBetweenDigits = String(e->argv[1]).toInt();
        }
        _renderCount = _renderWaitTime = _renderTime = 0;
        DEBUG_MSG_P(PSTR("delayBetweenDigits=%d\n"), _delayBetweenDigits);
        terminalOK();
    });

    terminalRegisterCommand(F("disp"), [](Embedis *e) {
        int value;
        if (e->argc > 1) {
            value = String(e->argv[1]).toInt();

            _mode = NIXIE_MODE_NONE;
            _writeValue(value);

            if (e->argc > 2) {
                int nixie = String(e->argv[2]).toInt();
                _turnNixieOn(NIXIE_BINARY_POSITION(nixie + 1));
                DEBUG_MSG_P(PSTR("value=%d on nixie=%d\n"), value, nixie);
            } else {
                DEBUG_MSG_P(PSTR("value=%d on all\n"), value);
                _turnAllNixiesOn();
            }
        }

        _printStatus();
        terminalOK();
    });

    terminalRegisterCommand(F("mode"), [](Embedis *e) {
        if (e->argc > 1) {
            int value = String(e->argv[1]).toInt();
            _setMode(value);
        }

        _printStatus();
        terminalOK();
    });
}

#endif // TERMINAL_SUPPORT

#if MQTT_SUPPORT

void _nixieMqttCallback(unsigned int type, const char *topic, const char *payload) {
    if (type == MQTT_CONNECT_EVENT) {
        mqttSubscribe(MQTT_TOPIC_NIXIE_MODE);
        mqttSubscribe(MQTT_TOPIC_NIXIE_DISPLAY);
    } else if (type == MQTT_MESSAGE_EVENT) {
        String t = mqttMagnitude((char *)topic);

        if (t.equals(MQTT_TOPIC_NIXIE_MODE)) {
            _setMode(atoi(payload));
        } else if (t.equals(MQTT_TOPIC_NIXIE_DISPLAY)) {
            _setMode(NIXIE_MODE_STATIC);
            _display(atoi(payload));
        }
    }
}

#endif // MQTT_SUPPORT

#if API_SUPPORT

void _nixieAPISetup() {
    apiRegister(
        MQTT_TOPIC_NIXIE_MODE, [](char *buffer, size_t len) { snprintf_P(buffer, len, PSTR("%d"), _mode); },
        [](const char *payload) { _setMode(atoi(payload)); });

    apiRegister(
        MQTT_TOPIC_NIXIE_DISPLAY, [](char *buffer, size_t len) { snprintf_P(buffer, len, PSTR("OK")); },
        [](const char *payload) {
            _setMode(NIXIE_MODE_STATIC);
            _display(atoi(payload));
        });
}

#endif // API_SUPPORT

void _nixieLoop() {
    if (_mode == NIXIE_MODE_NONE || _updating) {
        return;
    }
    if (Update.isRunning()) {
        _updating = true;
        _turnAllNixiesOn();
        _writeValue(9);
        return;
    }

    unsigned long ts = millis();

    _renderCount++;
    if (_renderCount > 3000) {
        _renderCount = 1;
        _renderWaitTime = _renderTime = 0;
    }
    _renderWaitTime += ts - _lastDraw;
    _displayDuration += ts - _lastDraw;

    // We start in demo mode , _updateDemoFrame switches the mode to clock once WiFi has been connected.
    if (_mode == NIXIE_MODE_DEMO) {
        _updateDemoFrame(ts);
    }

    switch (_mode) {
    case NIXIE_MODE_COUNT:
        _updateCountFrame(ts);
        break;
    case NIXIE_MODE_CLOCK:
        _updateDateTime(ts);
        // Swap date and time every DATETIME_SWAP_DURATION
        if (_displayDuration > DATETIME_SWAP_DURATION) {
            _displayDuration = 0;
            _showingDate = !_showingDate;
        }
        _copyToBuffer(_showingDate ? _dateValue : _timeValue);
        break;
    case NIXIE_MODE_IP:
        _updateIpFrame(ts);
        break;
    case NIXIE_MODE_TIMELEFT:
        _updateTimeLeft(ts);
        _copyToBuffer(_timeValue);
        break;
    case NIXIE_MODE_STATIC:
        break;
    }

    for (int i = 0; i < NUMBER_OF_NIXIES; i++) {
        int valueToWrite = _buffer[i];
        if (valueToWrite < 0) {
            _turnAllNixiesOff();
        } else {
            _writeValue(valueToWrite);

            _turnAllNixiesOff(); // Blanking
            delayMicroseconds(100);

            _turnNixieOn(NIXIE_BINARY_POSITION(i));
        }

        delayMicroseconds(_delayBetweenDigits);
    }

    // Capture times
    _lastDraw = ts;
    _renderTime += millis() - ts;

    // Automatic cathode anti-poisoning ?
}

void nixieSetup() {
    pinMode(LATCH_PIN, OUTPUT);
    pinMode(DATA_PIN, OUTPUT);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(NIXIE_A, OUTPUT);
    pinMode(NIXIE_B, OUTPUT);
    pinMode(NIXIE_C, OUTPUT);
    pinMode(NIXIE_D, OUTPUT);

    // https://www.forward.com.au/pfod/ESP8266/GPIOpins/ESP8266_01_pin_magic.html
    pinMode(3, INPUT_PULLUP);

    // Default values
    for (int i = 0; i < NUMBER_OF_NIXIES; i++) {
        _timeValue[i] = _dateValue[i] = NUMBER_OF_NIXIES - i;
    }

    _dateFormat = getSetting("dateFormat", DATEFORMAT_MMDDYY).toInt();
    _delayBetweenDigits = DELAY_BETWEEN_DIGITDISPLAY;
    _showTimeWhenConnected = true;
    _initDemoMode(); // Start in demo, switch to clock when connected

#if MQTT_SUPPORT
    mqttRegister(_nixieMqttCallback);
#endif

#if API_SUPPORT
    _nixieAPISetup();
#endif

    espurnaRegisterLoop(_nixieLoop);

#if TERMINAL_SUPPORT
    _nixieTerminal();
#endif
}