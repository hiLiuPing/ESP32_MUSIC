// #include <Arduino.h>
// #include <SPI.h>
// #include "ST73XX_UI.h"
// #include "ST7305_2p9_BW_DisplayDriver.h"
// #include "U8g2_for_ST73XX.h"
// #include "MyIMG.h"  //图标文件
// // #include "OneButton.h"


// //板子显示接线
// #define DC_PIN  5
// #define RES_PIN 4
// #define CS_PIN  3
// #define SCLK_PIN 6
// #define SDIN_PIN 1




// ST7305_2p9_BW_DisplayDriver display(DC_PIN, RES_PIN, CS_PIN, SCLK_PIN, SDIN_PIN, SPI);
// U8G2_FOR_ST73XX u8g2Fonts;

//         void zhuyetubiao(uint8_t i){

//             display.drawBitmap(19,(display.height() - 64) / 2,yuedu, 64, 64, 1);
//             display.drawBitmap(19,((display.height() - 64) / 2) +64,yueduzi, 64, 32, 1);
//             display.drawBitmap(113,(display.height() - 64) / 2,tianqi, 64, 64, 1);
//             display.drawBitmap(113,((display.height() - 64) / 2) +64,tianqizi, 64, 32, 1);
//             display.drawBitmap(207,(display.height() - 64) / 2,yinyue, 64, 64, 1);
//             display.drawBitmap(207,((display.height() - 64) / 2) +64,yinyuezi, 64, 32, 1);
//             display.drawBitmap(301,(display.height() - 64) / 2,shezhi, 64, 64, 1);
//             display.drawBitmap(301,((display.height() - 64) / 2 )+64,shezhizi, 64, 32, 1);

//             if(i==1){display.drawFilledRectangle(19, 149, 83, 154, ST7305_COLOR_BLACK);}  //两点法，左上角，右下角
//             else if(i==2){display.drawFilledRectangle(113, 149, 177, 154, ST7305_COLOR_BLACK);}  
//             else if(i==3){display.drawFilledRectangle(207, 149, 271, 154, ST7305_COLOR_BLACK);}  
//             else if(i==4){display.drawFilledRectangle(301, 149, 365, 154, ST7305_COLOR_BLACK);}  

//             }
// #define LED_PIN 48
// void setup() {
//     Serial.begin(115200);
//     Serial.println("Hello Arduino!");
//     // auto_eeprom();
//      pinMode(LED_PIN, OUTPUT);

//    display.initialize();
//     //display.Low_Power_Mode();
//     display.High_Power_Mode();
//    display.display_on(true);
//    display.display_Inversion(false);

//    u8g2Fonts.begin(display);                 // connect u8g2 procedures to ST73XX
//     //u8g2_for_st73xx.setFontDirection(1); 
//    display.setRotation(1);
//    display.clearDisplay();
//             display.drawBitmap(19,(display.height() - 64) / 2,yuedu, 64, 64, 1);
//             display.drawBitmap(19,((display.height() - 64) / 2) +64,yueduzi, 64, 32, 1);
//             display.drawBitmap(113,(display.height() - 64) / 2,tianqi, 64, 64, 1);
//             display.drawBitmap(113,((display.height() - 64) / 2) +64,tianqizi, 64, 32, 1);
//             display.drawBitmap(207,(display.height() - 64) / 2,yinyue, 64, 64, 1);
//             display.drawBitmap(207,((display.height() - 64) / 2) +64,yinyuezi, 64, 32, 1);
//             display.drawBitmap(301,(display.height() - 64) / 2,shezhi, 64, 64, 1);
//             display.drawBitmap(301,((display.height() - 64) / 2 )+64,shezhizi, 64, 32, 1);
//    display.display();







// }
// void loop() {


//     // 处理音频播放
//   // 点亮LED
//   digitalWrite(LED_PIN, HIGH);
//   // 延时1秒
//   delay(1000);
//   // 熄灭LED
//   digitalWrite(LED_PIN, LOW);
//   // 延时1秒
//   delay(1000);



// }




// // #include <Arduino.h>
// // #include <SPI.h>
// // #include <SD.h>
// // #include <WM8978.h>
// // #include <Audio.h>
// // #include <Button2.h>
// // #include <vector>
// // #include <Wire.h>
// #include <SD.h>
// #include "SdFat.h"

// // 定义自定义 SPI 引脚
// #define SD_MISO 41
// #define SD_MOSI 39
// #define SD_SCK  40
// #define SD_CS   38

// // // IIS初始化引脚定义
// // #define I2S_DOUT 12
// // #define I2S_BCLK 14
// // #define I2S_WS 15
// // #define I2S_MCLKPIN 11
// // #define I2S_DIN 13

// // IIC引脚定义
// #define I2C_SDA 17
// #define I2C_SCL 16

// #define RADIO_POW 18
// #define NS_CON 21
// #define PHONE_EN 47


// // Audio audio;
// // WM8978 dac;
// // // 创建自定义 SPI 实例
// // // SPIClass spi(HSPI);  // 用 HSPI 只是为了编号，不代表固定引脚
// // // SdFs sd;  // ✅ 使用 SdFs 而不是 SdFat
// SPIClass hspi(HSPI);
// SdFat sd;
// // uint8_t currentIndex=0; 
// // int a = 0;
// // void setup() {
// //   Serial.begin(115200);
// //   Serial.println("begin"); 
// //     pinMode(RADIO_POW, OUTPUT);
// //   digitalWrite(RADIO_POW, HIGH);  
// // pinMode(47, INPUT);

//   //        hspi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
//   //     if(!sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, 18000000,&hspi)))
//   //      {
//   //      Serial.println("SD卡初始化失败！");
//   //       return;
//   //       } 
//   // Serial.println("SD卡初始化成功！");   
//   // 使用自定义 SPI 初始化 SD 卡
// //   if (!SD.begin(SD_CS, spi)) {
// //     Serial.println("❌ SD 卡初始化失败！");
// //     return;
// //   }

// // Serial.println("SDok！");
//   //  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
//   // SPI.setFrequency(8000000);

//   // if (!SD.begin(SD_CS)) {
//   //   Serial.println("SD Card initialization failed!");
//   // } else {
//   //   Serial.println("SD Card initialized.");
//   // }
 
//   // dac.begin(I2C_SDA, I2C_SCL);
//   //  audio.setPinout(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLKPIN);
//   // dac.setSPKvol(40);
//   // dac.setHPvol(40, 40);
//   // audio.connecttoFS(SD, ("/a.mp3"));


// // }

// // void loop() {
// //     // audio.loop();
// //    Serial.println(digitalRead(47));
// //    delay(50);

// // }

#include <Arduino.h>
#include <SPI.h>
#include "ST73XX_UI.h"
#include "ST7305_2p9_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"
#include "MyIMG.h"  //图标文件
// #include "OneButton.h"

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
//板子显示接线
#define DC_PIN  5
#define RES_PIN 4
#define CS_PIN  3
#define SCLK_PIN 6
#define SDIN_PIN 1

#include "SD.h"
#include "FS.h"


#include <Arduino.h>
#include <SPI.h>
// #include <SD.h>
#include <WM8978.h>
#include <Audio.h>
#include <Button2.h>
#include <vector>
#include <Wire.h>
#include "SdFat.h"
// 定义自定义 SPI 引脚
#define SD_MISO 41
#define SD_MOSI 39
#define SD_SCK  40
#define SD_CS   38

// IIS初始化引脚定义
#define I2S_DOUT 12
#define I2S_BCLK 14
#define I2S_WS 15
#define I2S_MCLKPIN 11
#define I2S_DIN 13

// IIC引脚定义
#define I2C_SDA 17
#define I2C_SCL 16
// 音乐电源en
#define RADIO_POW 18
// 喇叭使能
#define NS_CON 21
// 耳机插入检测
#define PHONE_EN 47

#include "Adafruit_SHT4x.h"

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
#define LED_PIN 48

#include <WiFi.h>
#include <time.h>
#include "RTClib.h"

// ✅ 替换为你自己的 WiFi 名和密码
const char* ssid = "Redmi_1403";
const char* password = "15018762563";


// 设置时区，例如中国用东八区（UTC+8）
const char* ntpServer = "ntp.aliyun.com"; // 可换成其他如 "pool.ntp.org"
const long gmtOffset_sec = 8 * 3600;  // 东八区
const int daylightOffset_sec = 0;     // 无夏令时


// 创建 RTC 对象（PCF8563）
RTC_PCF8563 rtc;

#include <MPU6050.h>

MPU6050 mpu;

ST7305_2p9_BW_DisplayDriver display(DC_PIN, RES_PIN, CS_PIN, SCLK_PIN, SDIN_PIN, SPI);
U8G2_FOR_ST73XX u8g2Fonts;


Audio audio;
WM8978 dac;
// // 创建自定义 SPI 实例

// 创建自定义 SPI 实例
SPIClass hspi(HSPI);
SdFat sd;
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;
AudioOutputI2S *out;
// uint8_t currentIndex=0; 
void SDInit(){
         hspi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
      if(!sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, 18000000,&hspi)))
       {
       Serial.println("SD卡初始化失败！");
        return;
        } 
  Serial.println("SD卡初始化成功！");
}


// MAX17048G I2C 地址
#define MAX17048_ADDRESS 0x36

// 寄存器地址
#define MAX17048_VCELL   0x02
#define MAX17048_SOC     0x04
#define MAX17048_MODE    0x06
#define MAX17048_VERSION 0x08
#define MAX17048_CONFIG  0x0C
#define MAX17048_COMMAND 0xFE
void initMAX17048() {
  // 重置 MAX17048G
  Wire.beginTransmission(MAX17048_ADDRESS);
  Wire.write(MAX17048_COMMAND);
  Wire.write(0x54);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(10);
}
float readVoltage() {
  Wire.beginTransmission(MAX17048_ADDRESS);
  Wire.write(MAX17048_VCELL);
  Wire.endTransmission(false);
  
  Wire.requestFrom(MAX17048_ADDRESS, 2);
  byte msb = Wire.read();
  byte lsb = Wire.read();
  
  // 电压计算：寄存器值 * 78.125 μV
  float voltage = ((msb << 8) | lsb) * 78.125 / 1000000;
  return voltage;
}

float readPercentage() {
  Wire.beginTransmission(MAX17048_ADDRESS);
  Wire.write(MAX17048_SOC);
  Wire.endTransmission(false);
  
  Wire.requestFrom(MAX17048_ADDRESS, 2);
  byte msb = Wire.read();
  byte lsb = Wire.read();
  
  // 电量百分比计算：寄存器值 / 256
  float percent = (msb + (lsb / 256.0));
  return percent;
}

 
std::vector<std::string>  listmp3(std::string path);  //音乐文件夹列出
std::vector<std::string> mp3s;
std::vector<std::string> dirs;

FsFile folder,fileSY, fileSY2,txtFile,txtFile1,file1,dir,root,file4,file2,fileSY1,fileSY3,fileSY4; 

String musicpath ="/";  // 文件路径
std::vector<std::string> listmp3(std::string path) {

  std::vector<std::string> entries; 
  if (!dir.open(musicpath.c_str())) {
   Serial.println("dir.open failed");
   return entries; // 返回空数组
  }
  
  dir.rewind();//用于将目录中的文件列表指针重置到目录的开始位置。
  bool isDirEmpty = true; // 假设目录是空的
 // 检查目录是否为空
  if (file4.openNext(&dir, O_RDONLY)) {
    //Serial.println("不空的");
    isDirEmpty = false; // 目录不为空
  }

  if (isDirEmpty) {
    // 如果目录是空的，直接返回空数组
    //Serial.println("检查到空");
    return entries;
  }else{

  dir.rewind();//用于将目录中的文件列表指针重置到目录的开始位置。

    char buf[128];
  while (file4.openNext(&dir, O_RDONLY)) {
    
      // 检查文件扩展名是否为.bin
      file4.getName(buf, sizeof(buf));
      if (String(buf).endsWith(".mp3")) {
        // 打印文件名称
       // Serial.print("文件(.txt): ");
       // file.printName(&Serial);
       
         std::string name(buf);
        entries.push_back(name);
        // Serial.println(String(buf));
      }
    file4.close();
  }
  dir.close();
  return entries;
  }

}
// 递归遍历 SD 卡文件
// std::vector<String> allMusicFiles; 
void listFiles(SdFile& dir, int numTabs) {
    SdFile entry;
    while (entry.openNext(&dir, O_RDONLY)) {
        // 打印缩进
        for (int i = 0; i < numTabs; i++) {
            Serial.print("\t");
        }

        // 获取文件名
        char fileName[64];
        entry.getName(fileName, sizeof(fileName));
        Serial.println(fileName);
        
        // 如果是目录，则递归遍历
        if (entry.isDir()) {
            listFiles(entry, numTabs + 1);
        }
        entry.close();
    }
}
std::vector<String> allMusicFilesPath;  // 存放所有 MP3 文件
std::vector<String> allMusicFiles;  // 存放所有 MP3 文件
// void scanAllMusicFiles(String path);
// 递归扫描 SD 卡所有 MP3 文件
void scanAllMusicFiles(String path) {
    FsFile dir = sd.open(path);
    if (!dir) {
        Serial.println("Failed to open directory: " + path);
        return;
    }

    while (true) {
        FsFile entry = dir.openNextFile();
        if (!entry) {
            break;
        }

        // 使用 getName 方法获取文件名
        char fileName[256]; // 假设文件名最大长度为 255
        if (entry.getName(fileName, sizeof(fileName))) {
            if (entry.isDirectory()) {
                scanAllMusicFiles(path + "/" + fileName);  // 递归进入子文件夹
            } else if (String(fileName).endsWith(".mp3")) {
                allMusicFilesPath.push_back(path + "/" + fileName);
                allMusicFiles.push_back(String(fileName));
                Serial.println(fileName);
            }
        } else {
            Serial.println("Failed to get file name");
        }

        entry.close();
    }
    dir.close();
}
// 初始化时加载所有 MP3 到 “AllMusic” 播放列表
void loadAllMusic() {
    allMusicFiles.clear();
    allMusicFilesPath.clear();
    scanAllMusicFiles("/");
}


void setup() {
Serial.begin(115200);

// sht40
Serial.println("\nI2C Scanner");
Wire.begin(I2C_SDA, I2C_SCL);
// Wire.begin(I2C_SCL,I2C_SDA);
Wire.setClock(100000);
// Wire.setWireTimeout(3000,true);
 Wire.setTimeOut(3000);
// 获取i2c器件地址

// for (int i = 1; i<127;i++){
//    Wire.beginTransmission(i);
//    byte code = Wire.endTransmission();
//    if (code == 0)
//    {
//     Serial.print("Found: 0x");
//     Serial.println(i,HEX);
//    }
// }
//   delay(1000);

     pinMode(NS_CON, OUTPUT);
     pinMode(LED_PIN, OUTPUT);
   pinMode(RADIO_POW, OUTPUT);
  digitalWrite(RADIO_POW, HIGH);
  digitalWrite(NS_CON, HIGH);
 

   display.initialize();
    //display.Low_Power_Mode();
    display.High_Power_Mode();
   display.display_on(true);
   display.display_Inversion(false);

   u8g2Fonts.begin(display);                 // connect u8g2 procedures to ST73XX
    //u8g2_for_st73xx.setFontDirection(1); 
   display.setRotation(1);
   display.clearDisplay();
            display.drawBitmap(19,(display.height() - 64) / 2,yuedu, 64, 64, 1);
            display.drawBitmap(19,((display.height() - 64) / 2) +64,yueduzi, 64, 32, 1);
            display.drawBitmap(113,(display.height() - 64) / 2,tianqi, 64, 64, 1);
            display.drawBitmap(113,((display.height() - 64) / 2) +64,tianqizi, 64, 32, 1);
            display.drawBitmap(207,(display.height() - 64) / 2,yinyue, 64, 64, 1);
            display.drawBitmap(207,((display.height() - 64) / 2) +64,yinyuezi, 64, 32, 1);
            display.drawBitmap(301,(display.height() - 64) / 2,shezhi, 64, 64, 1);
            display.drawBitmap(301,((display.height() - 64) / 2 )+64,shezhizi, 64, 32, 1);
   display.display();


// SDInit();

  // SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  // SPI.setFrequency(8000000);
  // if (!SD.begin(SD_CS)) {
  //   Serial.println("SD Card initialization failed!");
  // } else {
  //   Serial.println("SD Card initialized.");
  // }
  hspi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS,hspi,40000000)) { // 这里的4是自定义的CS引脚号
      Serial.println("SD Card initialization failed!");
     return;
    } 
loadAllMusic();

  if (!sht4.begin()) {
    Serial.println("Couldn't find SHT4x sensor!");
    while (1) delay(1);
  }

  Serial.print("Found SHT4x sensor with ID: 0x");
  Serial.println(sht4.readSerial(), HEX);


  // 初始化 MAX17048G
  initMAX17048();
  
  Serial.println("MAX17048G 电量监测示例");


// 获取网络时间

  // 连接 WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
 //  点亮LED
  digitalWrite(LED_PIN, HIGH);
  // 初始化 NTP 对时
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // 等待时间同步完成
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Waiting for time");
    delay(1000);
  }
configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // 等待时间同步完成


  // 等待同步完成

  if (!getLocalTime(&timeinfo)) {
    Serial.println("时间同步失败！");
    return;
  }

  Serial.println("网络时间同步成功：");
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900,
                timeinfo.tm_mon + 1,
                timeinfo.tm_mday,
                timeinfo.tm_hour,
                timeinfo.tm_min,
                timeinfo.tm_sec);
 // 同步时间到rtc
 if (!rtc.begin()) {
    Serial.println("无法初始化 RTC（PCF8563）");
    while (1);
  }

  // 将时间写入 PCF8563
  rtc.adjust(DateTime(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  ));

  Serial.println("初始化 MPU6050...");
  mpu.initialize();

  if (mpu.testConnection()) {
    Serial.println("MPU6050 连接成功！");
  } else {
    Serial.println("MPU6050 连接失败！");
    while (1); // 停止程序
  }


  dac.begin(I2C_SDA, I2C_SCL);
  dac.setSPKvol(0);
  dac.setHPvol(20, 20);
  audio.setPinout(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLKPIN);
  audio.connecttoFS(SD, ("/a.mp3"));


  // dac.cfgADDA(1, 0); 
  //   dac.cfgInput(0, 0, 0); // 关闭输入
  // dac.cfgOutput(1, 0);   // 开启耳机输出
  // dac.setSPKvol(50);
  // dac.setHPvol(0, 0);
  // // --- 初始化 I2S 输出 ---
  // out = new AudioOutputI2S();
  // out->SetPinout(I2S_BCLK, I2S_WS, I2S_DOUT); // BCLK, LRC, DATA
  // out->SetGain(1);

  // file = new AudioFileSourceSD();
  // file->open("/a.mp3");
  // mp3 = new AudioGeneratorMP3();
  // mp3->begin(file, out);

}

void loop() {
  audio.loop(); 

  // if (mp3->isRunning()) {
  //   if (!mp3->loop()) {
  //     mp3->stop();
  //     Serial.println("播放完成");
  //   }
  // } else {
  //   delay(1000);
  // }



  // sensors_event_t humidity, temp;

//   // 触发一次测量（默认延迟为几十毫秒）
//   sht4.getEvent(&humidity, &temp);
// Serial.println("---------------------");

//   Serial.print("Temperature: ");
//   Serial.print(temp.temperature);
//   Serial.println(" °C");

//   Serial.print("Humidity: ");
//   Serial.print(humidity.relative_humidity);
//   Serial.println(" %");

// Serial.println("---------------------");


//   float voltage = readVoltage();
//   float percent = readPercentage();
  
//   Serial.print("电池电压: ");
//   Serial.print(voltage);
//   Serial.println(" V");
  
//   Serial.print("剩余电量: ");
//   Serial.print(percent);
//   Serial.println(" %");
  
//   Serial.println("---------------------");

// // delay(1000);  
   
 

//   //  RTC时间
//   DateTime now = rtc.now();

//   Serial.print("PCF8563 当前时间：");
//   Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
//                 now.year(), now.month(), now.day(),
//                 now.hour(), now.minute(), now.second());


//   int16_t ax, ay, az;
//   int16_t gx, gy, gz;

//   // 读取原始数据
//   mpu.getAcceleration(&ax, &ay, &az);
//   mpu.getRotation(&gx, &gy, &gz);

//   // 输出数据
//   Serial.print("加速度计: ax=");
//   Serial.print(ax);
//   Serial.print(" ay=");
//   Serial.print(ay);
//   Serial.print(" az=");
//   Serial.print(az);

//   Serial.print(" | 陀螺仪: gx=");
//   Serial.print(gx);
//   Serial.print(" gy=");
//   Serial.print(gy);
//   Serial.print(" gz=");
//   Serial.println(gz);

}
