#include <RCSwitch.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

Preferences preferences;  // Объект для работы с NVS
WebServer server(80);     // Веб-сервер на порту 80

// Пины для шагового мотора
#define IN1 2
#define IN2 3
#define IN3 4
#define IN4 5
// Пины для светодиодов
#define LED_GREEN 6
#define LED_BLUE 10
#define LED_RED 20
// Пин для кнопки
#define BUTTON_PIN 9
// Пин для приемника (RX)
#define RX_PIN 8
// Пин для передатчика (TX)
#define TX_PIN 7

// Состояние шлагбаума
bool isBarrierOpen = false;
bool winnerMode = false;

// Таймеры
unsigned long autoCloseTime = 0;
unsigned long codeWaitStartTime = 0;
const unsigned long AUTO_CLOSE_DELAY = 5000;    // Время автозакрытия шлагбаума
const unsigned long CODE_WAIT_TIMEOUT = 42000;  // Время ожидания ответа

// Коды
uint32_t currentExpectedCode = 0x00000000;  // Нулевой код по умолчанию
uint32_t lastValidCode = 0;

RCSwitch mySwitch = RCSwitch();

// Последовательность шагов
int stepSequence[8][4] = {
  { 1, 0, 0, 1 },
  { 0, 0, 0, 1 },
  { 0, 0, 1, 1 },
  { 0, 0, 1, 0 },
  { 0, 1, 1, 0 },
  { 0, 1, 0, 0 },
  { 1, 1, 0, 0 },
  { 1, 0, 0, 0 }
};

int stepDelay = 3;
const int stepsForFullMovement = 1024;

// Для мигания светодиодов
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 500;
bool ledState = false;

// Переменные для веб-интерфейса
bool isRoot = false;
String currentDir = "/home/user";
String currentUser = "anonymous";

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("===== КВЕСТ-ШЛАГБАУМ 1.0 =====");
  Serial.println("* Инициализируем EEPROM");
  preferences.begin("barrier", false);                                    // (false - режим чтения/записи)
  currentExpectedCode = preferences.getUInt("expectedCode", 0x00000000);  // Пытаемся загрузить сохраненный код
  Serial.print("< Загружен код из памяти: 0x00");
  Serial.println(currentExpectedCode, HEX);
  delay(500);
  Serial.println("* Настраиваем пины");
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(400);
  Serial.println("* Запускаем радиомодули");
  mySwitch.enableReceive(digitalPinToInterrupt(RX_PIN));
  mySwitch.enableTransmit(TX_PIN);
  mySwitch.setProtocol(1);
  mySwitch.setPulseLength(300);
  delay(500);
  Serial.println("* Проверяем RGB светодиод");
  digitalWrite(LED_GREEN, HIGH);
  delay(300);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, HIGH);
  delay(300);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_RED, HIGH);
  delay(300);
  digitalWrite(LED_RED, LOW);
  delay(700);
  Serial.println("* Включаем Wi-Fi точку доступа");
  setupWiFiAP();
  Serial.println("* Конфигурируем веб-сервер");
  setupWebServer();
  delay(1000);
  Serial.println("= КВЕСТ-ШЛАГБАУМ ИНИЦИАЛИЗИРОВАН =");
  Serial.println("~ Нажмите на кнопку для запуска квеста..");
  closeBarrier();
  waitingStartQuest();
}

void setupWiFiAP() {
  WiFi.softAP("ROTTEN_GATE", "12345678");
  delay(100);
  Serial.print("! Точка доступа ROTTEN_GATE запущена на ");
  Serial.println(WiFi.softAPIP());
  Serial.println("! Пароль: 12345678");
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/cmd", handleCommand);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleRoot() {
  String html = getTerminalHTML();
  server.send(200, "text/html", html);
}

void handleCommand() {
  if (server.hasArg("command")) {
    String command = server.arg("command");
    String response = processCommand(command);
    server.send(200, "text/plain", response);
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

String getTerminalHTML() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>RM BST</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }
        body {
        background-color: #000;
        color: #0f0;
        font-family: 'Cascadia Code', 'Consolas', 'Monaco', 'Lucida Console', monospace;
        height: 50vh;
        max-height: 50vh;
        overflow: hidden;
        position: fixed;
        top: 0;
        left: 0;
        right: 0;
        border: 2px solid #0f0;
        border-radius: 12px;
    }
        .terminal {
            background-color: #001100;
            height: 100%;
            display: flex;
            flex-direction: column;
            padding: 10px;
            border-radius: 10px;
        }
        .header {
            text-align: center;
            padding: 5px;
            border-bottom: 1px solid #0f0;
            background: rgba(0, 32, 0, 0.3);
            flex-shrink: 0;
            border-radius: 8px 8px 0 0;
        }
        .ascii-art {
        font-family: 'Cascadia Code', 'Consolas', 'Monaco', 'Lucida Console', monospace;
        font-size: 4px;
        line-height: 1.1;
        white-space: pre;
        margin: 0 auto;
    }
        .header p {
            font-size: 10px;
            margin-top: 3px;
        }
        .output-container {
            flex: 1;
            overflow-y: auto;
            padding: 8px;
            margin: 5px 0;
            background: rgba(0, 32, 0, 0.2);
            border-radius: 5px;
        }
        .output {
        font-family: 'Cascadia Code', 'Consolas', 'Monaco', 'Lucida Console', monospace;
        white-space: pre-wrap;
        word-wrap: break-word;
        font-size: 12px;
        line-height: 1.3;
    }
        .command-section {
            border-top: 1px solid #0f0;
            background: rgba(0, 32, 0, 0.5);
            padding: 8px;
            flex-shrink: 0;
            border-radius: 0 0 8px 8px;
        }
        .command-line {
            display: flex;
            align-items: center;
        }
        .prompt {
        font-family: 'Cascadia Code', 'Consolas', 'Monaco', 'Lucida Console', monospace;
        color: #0ff;
        font-weight: bold;
        font-size: 12px;
        margin-right: 8px;
        white-space: nowrap;
    }
        .command-input {
        font-family: 'Cascadia Code', 'Consolas', 'Monaco', 'Lucida Console', monospace;
        background: transparent;
        border: none;
        color: #0f0;
        font-size: 12px;
        width: 100%;
        outline: none;
        caret-color: #0f0;
    }
        .command-input::placeholder {
            color: #0a0;
        }
        .output-container::-webkit-scrollbar {
            width: 6px;
        }
        .output-container::-webkit-scrollbar-track {
            background: #002200;
            border-radius: 3px;
        }
        .output-container::-webkit-scrollbar-thumb {
            background: #00ff00;
            border-radius: 3px;
        }
        @media (min-height: 800px) {
            body {
                height: 400px;
                max-height: 400px;
            }
            .ascii-art {
                font-size: 4px;
            }
            .output {
                font-size: 14px;
            }
            .prompt, .command-input {
                font-size: 14px;
            }
        }
    </style>
</head>
<body>
    <div class="terminal">
        <div class="header">
            <div class="ascii-art">
    ____  ____    __    __  _______   __       __  _________________  _____    _   ___________ __  ___       _____   ________
   / __ \/ __ \__/ /_  / / / ____/ | / /      /  |/  / ____/ ____/ / / /   |  / | / /  _/ ___//  |/  /      /  _/ | / / ____/
  / /_/ / / / /_  __/_/ /_/ __/ /  |/ /      / /|_/ / __/ / /   / /_/ / /| | /  |/ // / \__ \/ /|_/ /       / //  |/ / /     
 / _, _/ /_/ / / / /_  _ / /___/ /|  /      / /  / / /___/ /___/ __  / ___ |/ /|  // / ___/ / /  / /      _/ // /|  / /___   
/_/ |_|\____/ /_/   /_/ /_____/_/ |_/      /_/  /_/_____/\____/_/ /_/_/  |_/_/ |_/___//____/_/  /_/      /___/_/ |_/\____/   
                                                                                                                             </div>
            <p>Barrier Security Terminal v1.3.37</p>
        </div>
        <div class="output-container" id="outputContainer">
            <div class="output" id="output"></div>
        </div>
        <div class="command-section">
            <div class="command-line">
                <span class="prompt">anonymous@rotten_inc:~$ </span>
                <input type="text" class="command-input" id="commandInput" autocomplete="off" autofocus placeholder=" ">
            </div>
        </div>
    </div>
    
    <script>
        const outputContainer = document.getElementById('outputContainer');
        const output = document.getElementById('output');
        const commandInput = document.getElementById('commandInput');
        
        function addOutput(text) {
            const newContent = document.createElement('div');
            newContent.style.marginBottom = '4px';
            newContent.innerHTML = text;
            output.appendChild(newContent);
            setTimeout(() => {
                outputContainer.scrollTop = outputContainer.scrollHeight;
            }, 10);
        }

        function executeCommand(cmd) {
            if (cmd.trim() === '') return;
            addOutput('<span style="color: #0ff;">anonymous@rotten_inc:~$</span> ' + cmd);
            fetch('/cmd?command=' + encodeURIComponent(cmd))
                .then(response => response.text())
                .then(data => {
                    if (data === 'CLEAR_SCREEN') {
                        output.innerHTML = '';
                    } else if (data !== '') {
                        addOutput(data);
                    }
                    commandInput.value = '';
                })
                .catch(error => {
                    addOutput('Ошибка: ' + error);
                    commandInput.value = '';
                });
        }
        commandInput.addEventListener('keypress', function(e) {
            if (e.key === 'Enter') {
                executeCommand(this.value);
            }
        });
        commandInput.focus();
        document.addEventListener('click', function() {
            commandInput.focus();
        });
        setTimeout(() => {
            addOutput('Добро пожаловать в Barrier Security System');
            addOutput('Уровень доступа: ОГРАНИЧЕННЫЙ');
            addOutput('Все операции мониторятся и логируются');
            addOutput('');
            addOutput('Для справки введите: <span style="color: #0ff;">help</span>');
            addOutput('');
        }, 100);
        
    </script>
</body>
</html>
)";
  return html;
}

String processCommand(String command) {
  command.trim();

  // Флаг, определяем выполняем ли команду как root (проверяем разные регистры)
  bool sudoMode = command.startsWith("sudo ") || command.startsWith("Sudo ");

  String actualCommand = command;
  if (sudoMode) {
    // Находим позицию первого пробела после sudo
    int spacePos = command.indexOf(' ');
    if (spacePos != -1) {
      actualCommand = command.substring(spacePos + 1);
    } else {
      // Если команда просто "sudo" без параметров
      actualCommand = "";
    }
  }

  String actualCommandLower = actualCommand;
  actualCommandLower.toLowerCase();

  if (command.equalsIgnoreCase("sudo") || command.equalsIgnoreCase("Sudo")) {
    return "anonymous is already in the sudoers file.";
  }
  if (command.equalsIgnoreCase("id") || command.equalsIgnoreCase("Id")) {
    return "uid=1001(anonymous) gid=1003(anonymous) groups=1003(anonymous)";
  }
  if (command.equalsIgnoreCase("cat") || command.equalsIgnoreCase("Cat")) {
    return "&lt;usage&gt; cat: cat myfile.txt";
  }
  if (actualCommandLower == "help" || actualCommand == "") {
    return R"(Доступные команды:
help      - показать эту справку
ls        - список файлов
cat       - просмотр файлов
clear     - очистить экран
)";
  } else if ((actualCommandLower == "ls") || (actualCommandLower == "ls -la")) {
    return R"(-rw-r--r--  1 user 288 Sep 28 08:22 codes.txt
-rw-------  1 root 916 Sep 28 08:30 key_hint.txt
-rw-------  1 root 732 Sep 28 08:33 memo.txt
-rw-r--r--  1 user 402 Sep 28 08:20 readme.txt
-rw-------  1 root 320 Sep 28 08:31 root_log.txt
-rw-r--r--  1 user 407 Sep 28 08:21 user_log.txt
)";
  } else if (actualCommandLower.startsWith("cat ")) {
    String filename = actualCommand.substring(4);  // Берем оригинальное имя файла
    String filenameLower = filename;
    filenameLower.toLowerCase();

    if (sudoMode) {
      // Чтение файлов root
      if ((filenameLower == "key_hint.txt") || (filenameLower == "key_hint")) {
        return R"(=== СЕКРЕТНО - ТОЛЬКО ДЛЯ АДМИНИСТРАТОРОВ ROTTEN MECHANISM ===

АНАЛИЗ ПАТТЕРНОВ ГЕНЕРАЦИИ КЛЮЧЕЙ ДОСТУПА

Обнаружена циклическая последовательность в протоколе доступа:
- Каждый байт ключа изменяется по определенному алгоритму
- Младший байт: инкремент на +1 (0x01 → 0x02 → 0x03...)
- Средний байт: инкремент на +2 (0x04 → 0x06 → 0x08...)
- Старший байт: инкремент на +4 (0x08 → 0x0C → 0x10...)

Пример последовательности:
0x00080402 → 0x000C0603 → 0x00100804 → ...

ПРИМЕЧАНИЕ: При достижении 0xFF происходит переполнение
(0xFF + 1 = 0x00, 0xFE + 2 = 0x00, 0xFD + 4 = 0x01)
)";
      } else if ((filenameLower == "memo.txt") || (filenameLower == "memo")) {
        return R"(СЛУЖЕБНАЯ ЗАПИСКА ROTTEN INC

Текущий уровень безопасности: ВЫСОКИЙ
ВНИМАНИЕ, система шлагбаума функционирует в 
тестовом режиме.

Коды доступа генерируются по особому алгоритму:

Для получения доступа и открытия шлагбаума необходимо 
предсказать следующий ключ в последовательности и 
передать его через RF-канал.

Все попытки доступа логируются.
Несанкционированные действия будут пресекаться.
)";
      } else if ((filenameLower == "root_log.txt") || (filenameLower == "root_log")) {
        return R"([2025-28-09 08:30:01] ROOT: Система запущена
[2025-28-09 08:35:22] ROOT: Проверка безопасности пройдена
[2025-28-09 08:40:15] ROOT: Резервное копирование завершено
[2025-28-09 08:45:30] ROOT: Мониторинг активности: норма
)";
      } else {
        return "cat: " + filename + ": No such file in root directory";
      }
    } else {
      // Чтение файлов пользователя
      if ((filenameLower == "readme.txt") || (filenameLower == "readme")) {
        return R"(Добро пожаловать в Barrier Security System

Эта система содержит информацию о механизмах контроля доступа.
Все операции логируются. Несанкционированный доступ запрещен.

Для доступа к [ДАННЫЕ УДАЛЕНЫ] требуются █████ ████. 
)";
      } else if ((filenameLower == "codes.txt") || (filenameLower == "codes")) {
        return R"(Контроль доступа BSS

Коды доступа обновляются по [ДАННЫЕ УДАЛЕНЫ] алгоритму.
Для получения █████ ████ используйте команды: [НЕДОСТАТОЧНО ПРАВ ДОСТУПА]

)";
      } else if ((filenameLower == "user_log.txt") || (filenameLower == "user_log")) {
        return R"([2025-27-09 08:30:01] SYSTEM: Запуск системы BSS
[2025-27-09 08:31:15] USER: Попытка доступа к documents/
[2025-27-09 08:32:30] SECURITY: Обнаружена аномальная активность
[2025-27-09 08:33:45] SYSTEM: Резервное копирование завершено
[2025-27-09 08:34:10] ACCESS: Попытка получения root-доступа
)";
      } else if ((filenameLower == "root_log.txt") || (filenameLower == "root_log")) {
        return "cat: root_log.txt: Permission denied";
      } else if ((filenameLower == "memo.txt") || (filenameLower == "memo")) {
        return "cat: memo.txt: Permission denied";
      } else if ((filenameLower == "key_hint.txt") || (filenameLower == "key_hint")) {
        return "cat: key_hint.txt: Permission denied";
      } else {
        return "cat: " + filename + ": No such file or directory";
      }
    }
  } else if (actualCommandLower == "clear") {
    return "CLEAR_SCREEN";
  } else if (actualCommandLower == "exit") {
    return "There is no ESCAPE ^^";
  } else {
    return "command not found: " + command + "\nВведите 'help' для списка команд";
  }
}

void loop() {
  // 0. Обрабатываем веб-запросы
  server.handleClient();

  // 1. Проверяем команду по радио
  if (!winnerMode && mySwitch.available()) {
    uint32_t receivedValue = mySwitch.getReceivedValue();

    Serial.print("< Получен код: 0x00");
    Serial.println(receivedValue, HEX);
    Serial.print("~ Ожидался код: 0x00");
    Serial.println(currentExpectedCode, HEX);

    if (receivedValue == currentExpectedCode) {
      Serial.println("+ КОД ПРИНЯТ!");
      handleValidCode(receivedValue);
    } else {
      Serial.println("× НЕВЕРНЫЙ КОД");
      blinkRedError();  // Красный светодиод для ошибки
    }
    mySwitch.resetAvailable();
  }

  // 2. Проверяем таймаут ожидания кода
  if (!winnerMode && codeWaitStartTime > 0 && millis() - codeWaitStartTime >= CODE_WAIT_TIMEOUT) {
    Serial.print("! Код не получен в течение ");
    Serial.print(CODE_WAIT_TIMEOUT / 1000);
    Serial.println(" секунд, он больше не валиден");
    blinkBlueTimeout();             // Синий светодиод для таймаута
    sendCode(currentExpectedCode);  // Отправляем текущий код
    generateNextExpectedCode();     // Пропускаем текущий код и генерируем следующий
    startCodeWaiting();             // Начинаем ожидание следующего кода
  }

  // 3. Проверяем кнопку
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);  // Защита от дребезга
    if (digitalRead(BUTTON_PIN) == LOW) {
      if (winnerMode) {
        sendCode(currentExpectedCode);  // Отправляем текущий код
        Serial.println("~ Нажата кнопка - закрываем шлагбаум после победы");
        closeBarrierAfterWin();
      } else {
        Serial.println("~ Нажата кнопка - ручное управление");
        toggleBarrier();
      }
      // Ждем отпускания кнопки
      while (digitalRead(BUTTON_PIN) == LOW) delay(10);
    }
  }

  // 4. Проверяем автозакрытие
  if (!winnerMode && isBarrierOpen && autoCloseTime > 0 && millis() - autoCloseTime >= AUTO_CLOSE_DELAY) {
    Serial.println("~ Автоматическое закрытие..");
    closeBarrier();
  }

  // 5. Управляем миганием светодиодов
  handleLEDBlinking();
}


void startCodeWaiting() {
  codeWaitStartTime = millis();
  Serial.print("~ Ожидание кода 0x00");
  Serial.print(currentExpectedCode, HEX);
  Serial.print(" в течение ");
  Serial.print(CODE_WAIT_TIMEOUT / 1000);
  Serial.println(" секунд...");

  // Гасим все светодиоды при начале ожидания
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_RED, LOW);
}

void waitingStartQuest() {
  while (digitalRead(BUTTON_PIN) == 1) {
    delay(10);
  }
  sendCode(currentExpectedCode);  // Отправляем первый код
  Serial.println("~ Квест запущен! Последовательность активирована, веб-сервер работает");
  blinkGreenAcknowledge();
  generateNextExpectedCode();  // Сразу генерируем следующий код
  toggleBarrier();
  startCodeWaiting();
}

void handleValidCode(uint32_t code) {
  lastValidCode = code;
  Serial.println("= УРА, У НАС ЕСТЬ ПОБЕДИТЕЛЬ! =");
  winnerMode = true;
  openBarrierForWinner();
}

void generateNextExpectedCode() {
  // Извлекаем байты текущего кода
  uint8_t byte1 = (currentExpectedCode >> 16) & 0xFF;
  uint8_t byte2 = (currentExpectedCode >> 8) & 0xFF;
  uint8_t byte3 = currentExpectedCode & 0xFF;

  // Увеличиваем согласно алгоритму
  byte1 = (byte1 + 4) & 0xFF;
  byte2 = (byte2 + 2) & 0xFF;
  byte3 = (byte3 + 1) & 0xFF;

  // Формируем новый код
  currentExpectedCode = ((uint32_t)byte1 << 16) | ((uint32_t)byte2 << 8) | byte3;

  // Сохраняем в энергонезависимую память
  preferences.putUInt("expectedCode", currentExpectedCode);

  Serial.print("! Следующий ожидаемый код: 0x00");
  Serial.print(currentExpectedCode, HEX);
  Serial.println(" сохранен в памяти");
}

void sendCode(uint32_t code) {
  Serial.print("> Отправка кода: 0x00");
  Serial.println(code, HEX);
  mySwitch.send(code, 24);
  delay(100);
}

void blinkGreenAcknowledge() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_GREEN, HIGH);
    delay(200);
    digitalWrite(LED_GREEN, LOW);
    delay(200);
  }
}

void blinkBlueTimeout() {
  // Мигаем синим 3 раза при таймауте
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BLUE, HIGH);
    delay(200);
    digitalWrite(LED_BLUE, LOW);
    delay(200);
  }
}

void blinkRedError() {
  // Мигаем красным 3 раза при неверном коде
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_RED, HIGH);
    delay(100);
    digitalWrite(LED_RED, LOW);
    delay(100);
  }
}

void openBarrierForWinner() {
  if (isBarrierOpen) return;
  // Мигаем зеленым во время открытия


  for (int i = 0; i < stepsForFullMovement; i++) {
    executeStep(true);

    if (millis() - lastBlinkTime >= 100) {
      ledState = !ledState;
      digitalWrite(LED_GREEN, ledState);
      lastBlinkTime = millis();
    }
  }

  stopMotor();
  isBarrierOpen = true;
  Serial.println("| Шлагбаум открыт для победителя!");
  Serial.println("~ Для продолжения квеста - нажмите на кнопку");
}

void closeBarrierAfterWin() {
  Serial.println("! Завершение режима победителя...");
  sendCode(currentExpectedCode);  // Отправляем текущий ожидаемый код
  generateNextExpectedCode();     // Генерируем следующий код для нового участника
  closeBarrier();                 // Закрываем шлагбаум
  winnerMode = false;             // Возвращаем в обычный режим
  startCodeWaiting();
}

void toggleBarrier() {
  if (isBarrierOpen) {
    closeBarrier();
  } else {
    openBarrier();
  }
}

void openBarrier() {
  if (isBarrierOpen || winnerMode) return;

  Serial.println("! Открываю шлагбаум...");

  lastBlinkTime = millis();
  ledState = true;
  digitalWrite(LED_GREEN, ledState);

  for (int i = 0; i < stepsForFullMovement; i++) {
    executeStep(true);

    if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
      ledState = !ledState;
      digitalWrite(LED_GREEN, ledState);
      lastBlinkTime = millis();
    }
  }

  stopMotor();
  isBarrierOpen = true;
  autoCloseTime = millis();
  Serial.println("| Шлагбаум открыт");
}

void closeBarrier() {
  if (!isBarrierOpen) return;

  Serial.println("! Закрываю шлагбаум...");

  // Гасим все светодиоды
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_RED, LOW);

  for (int i = 0; i < stepsForFullMovement; i++) {
    executeStep(false);
  }

  stopMotor();
  isBarrierOpen = false;
  autoCloseTime = 0;
  Serial.println("— Шлагбаум закрыт");
}

void executeStep(bool direction) {
  static int currentStep = 0;

  if (direction) {
    currentStep++;
    if (currentStep >= 8) currentStep = 0;
  } else {
    currentStep--;
    if (currentStep < 0) currentStep = 7;
  }

  digitalWrite(IN1, stepSequence[currentStep][0]);
  digitalWrite(IN2, stepSequence[currentStep][1]);
  digitalWrite(IN3, stepSequence[currentStep][2]);
  digitalWrite(IN4, stepSequence[currentStep][3]);

  delay(stepDelay);
}

void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void handleLEDBlinking() {
  if (winnerMode && isBarrierOpen) {
    // В режиме победителя быстро мигаем зеленым
    if (millis() - lastBlinkTime >= 300) {
      ledState = !ledState;
      digitalWrite(LED_GREEN, ledState);
      lastBlinkTime = millis();
    }
  } else if (isBarrierOpen && !winnerMode) {
    // Обычное открытое состояние - медленное мигание зеленым
    if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
      ledState = !ledState;
      digitalWrite(LED_GREEN, ledState);
      lastBlinkTime = millis();
    }
  }
}

// Функция для сброса сохраненного кода (по желанию)
void resetSavedCode() {
  preferences.putUInt("expectedCode", 0x00080402);
  Serial.println("! Код сброшен к начальному значению");
}

// В деструкторе закрываем preferences
void cleanup() {
  preferences.end();
}