#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEClient.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include <esp_log.h>
#include <esp_bt_main.h>
#include <string>
#include "Task.h"
#include <sys/time.h>
#include <time.h>
#include "sdkconfig.h"
#include <Wire.h>
#include <U8g2lib.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

static char LOG_TAG[] = "SampleServer";

static BLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static BLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static BLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static BLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");

uint8_t latestMessageID[4];
boolean pendingNotification = false;
boolean incomingCall = false;
uint8_t acceptCall = 0;

// Biến để lưu thông tin thông báo
String notifTitle = "";
String notifMessage = "";
String notifCategory = "";
String notifDate = "";

// Hàm chuyển đổi tiếng Việt có dấu thành không dấu
String removeAccents(String input)
{

    String accents[][6] = {
        {"á", "à", "ả", "ã", "ạ", "a"},
        {"â", "ấ", "ầ", "ẩ", "ẫ", "a"},
        {"ă", "ắ", "ằ", "ẳ", "ẵ", "a"},
        {"ậ", "ặ", "", "", "", "a"},
        {"đ", "", "", "", "", "d"},
        {"é", "è", "ẻ", "ẽ", "ẹ", "e"},
        {"ê", "ế", "ề", "ể", "ễ", "e"},
        {"ệ", "", "", "", "", "e"},
        {"í", "ì", "ỉ", "ĩ", "ị", "i"},
        {"ó", "ò", "ỏ", "õ", "ọ", "o"},
        {"ô", "ố", "ồ", "ổ", "ỗ", "o"},
        {"ơ", "ớ", "ờ", "ở", "ỡ", "o"},
        {"ộ", "ợ", "", "", "", "o"},
        {"ú", "ù", "ủ", "ũ", "ụ", "u"},
        {"ư", "ứ", "ừ", "ử", "ữ", "u"},
        {"ự", "", "", "", "", "u"},
        {"ý", "ỳ", "ỷ", "ỹ", "ỵ", "y"},
        {"Á", "À", "Ả", "Ã", "Ạ", "A"},
        {"Â", "Ấ", "Ầ", "Ẩ", "Ẫ", "A"},
        {"Ă", "Ắ", "Ằ", "Ẳ", "Ẵ", "A"},
        {"Ậ", "Ặ", "", "", "", "A"},
        {"Đ", "", "", "", "", "D"},
        {"É", "È", "Ẻ", "Ẽ", "Ẹ", "E"},
        {"Ê", "Ế", "Ề", "Ể", "Ễ", "E"},
        {"Ệ", "", "", "", "", "E"},
        {"Í", "Ì", "Ỉ", "Ĩ", "Ị", "I"},
        {"Ó", "Ò", "Ỏ", "Õ", "Ọ", "O"},
        {"Ô", "Ố", "Ồ", "Ổ", "Ỗ", "O"},
        {"Ơ", "Ớ", "Ờ", "Ở", "Ỡ", "O"},
        {"Ộ", "Ợ", "", "", "", "O"},
        {"Ú", "Ù", "Ủ", "Ũ", "Ụ", "U"},
        {"Ư", "Ứ", "Ừ", "Ử", "Ữ", "U"},
        {"Ự", "", "", "", "", "U"},
        {"Ý", "Ỳ", "Ỷ", "Ỹ", "Ỵ", "Y"}};

    String result = input;

    // Thay thế các ký tự có dấu
    for (int i = 0; i < 13; i++)
    {
        for (int j = 0; j < 5; j++)
        {
            if (accents[i][j] != "")
            {
                result.replace(accents[i][j], accents[i][5]);
            }
        }
    }

    return result;
}

void clearDisplay() {
    u8g2.setDrawColor(0); // Màu vẽ = 0 (đen/off)
    u8g2.drawBox(0, 9, 128, 55); // Xóa vùng hiển thị thông báo
    u8g2.setDrawColor(1); // Đặt lại màu vẽ = 1 (trắng/on)
}

// Hàm hiển thị thông báo lên màn hình OLED
void displayNotification()
{
    clearDisplay();

    // Hiển thị tiêu đề
    u8g2.setFont(u8g2_font_NokiaSmallBold_te);
    String titleLine = notifTitle.substring(0, 21);
    u8g2.drawStr(0, 23, titleLine.c_str());

    // Hiển thị nội dung tin nhắn trong 3 dòng
    u8g2.setFont(u8g2_font_6x10_tf);
    if (notifMessage.length() > 0)
    {
        // Dòng 1
        String msgLine1 = notifMessage.length() > 21 ? notifMessage.substring(0, 21) : notifMessage;
        u8g2.drawStr(0, 37, msgLine1.c_str()); // Di chuyển lên trên (từ 36 xuống 25)

        // Dòng 2 (nếu có)
        if (notifMessage.length() > 21)
        {
            String msgLine2 = notifMessage.length() > 42 ? notifMessage.substring(21, 42) : notifMessage.substring(21);
            u8g2.drawStr(0, 47, msgLine2.c_str()); // Di chuyển lên trên (từ 46 xuống 40)

            // Dòng 3 (nếu có)
            if (notifMessage.length() > 42)
            {
                String msgLine3 = notifMessage.substring(42, min((int)notifMessage.length(), 63));
                u8g2.drawStr(0, 57, msgLine3.c_str());
            }
        }
    }

    u8g2.sendBuffer();
}

// Hiển thị màn hình cuộc gọi đến
void displayIncomingCall()
{   
    clearDisplay();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, "CO CUOC GOI DEN");
    u8g2.drawStr(0, 30, notifTitle.c_str());
    u8g2.drawStr(0, 50, "1:Tra loi   0:Tu choi");
    u8g2.sendBuffer();
}

// Hiển thị màn hình chờ
void displayStandby()
{   
    clearDisplay();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 24, "Da ket noi");
    u8g2.drawStr(0, 40, "Dang cho thong bao");
    u8g2.sendBuffer();
}

// Hiển thị pin
void displayBattery(int percent)
{
    u8g2.setFont(u8g2_font_5x8_tf);

    u8g2.drawFrame(0, 1, 14, 6);
    u8g2.drawBox(14, 2, 2, 4);

    int fillWidth = map(percent, 0, 100, 0, 12);
    if (fillWidth > 0)
    {
        u8g2.drawBox(1, 2, fillWidth, 4);
    }

    String text = String(percent) + "%";
    u8g2.drawStr(18, 7, text.c_str());

    u8g2.sendBuffer();
}

class MySecurity : public BLESecurityCallbacks
{

    uint32_t onPassKeyRequest()
    {
        ESP_LOGI(LOG_TAG, "PassKeyRequest");
        return 123456;
    }

    void onPassKeyNotify(uint32_t pass_key)
    {
        ESP_LOGI(LOG_TAG, "On passkey Notify number:%d", pass_key);
    }

    bool onSecurityRequest()
    {
        ESP_LOGI(LOG_TAG, "On Security Request");
        return true;
    }

    bool onConfirmPIN(unsigned int)
    {
        ESP_LOGI(LOG_TAG, "On Confrimed Pin Request");
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
    {
        ESP_LOGI(LOG_TAG, "Starting BLE work!");
        if (cmpl.success)
        {
            uint16_t length;
            esp_ble_gap_get_whitelist_size(&length);
            ESP_LOGD(LOG_TAG, "size: %d", length);
        }
    }
};

static void dataSourceNotifyCallback(
    BLERemoteCharacteristic *pDataSourceCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{

    // Xác định loại thông tin đang nhận
    if (length > 8)
    {
        uint8_t attributeID = pData[5];

        // Tạo chuỗi tạm để lưu dữ liệu
        char tempBuffer[100] = {0};
        int dataLength = length - 8;
        if (dataLength > 99)
            dataLength = 99;

        for (int i = 0; i < dataLength; i++)
        {
            tempBuffer[i] = pData[i + 8];
        }
        tempBuffer[dataLength] = '\0';

        String tempStr = String(tempBuffer);
        // Chuyển đổi thành không dấu
        tempStr = removeAccents(tempStr);

        // Phân loại và lưu dữ liệu theo ID
        switch (attributeID)
        {
        case 0x01: // Tiêu đề
            notifTitle = tempStr;
            Serial.print("Tieu de: ");
            break;
        case 0x03: // Tin nhắn
            notifMessage = tempStr;
            Serial.print("Noi dung: ");
            break;
        case 0x05: // Ngày
            notifDate = tempStr;
            Serial.print("Ngay: ");
            break;
        default:
            break;
        }

        Serial.println(tempStr);

        // Hiển thị thông báo sau khi nhận đủ dữ liệu
        if (incomingCall)
        {
            displayIncomingCall();
        }
        else if (!notifTitle.isEmpty())
        {
            displayNotification();
        }
    }

    Serial.println();
}

static void NotificationSourceNotifyCallback(
    BLERemoteCharacteristic *pNotificationSourceCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
    if (pData[0] == 0)
    {
        Serial.println("New notification!");
        // Serial.println(pNotificationSourceCharacteristic->getUUID().toString().c_str());
        latestMessageID[0] = pData[4];
        latestMessageID[1] = pData[5];
        latestMessageID[2] = pData[6];
        latestMessageID[3] = pData[7];

        // Xóa các thông tin cũ
        notifTitle = "";
        notifMessage = "";
        notifDate = "";

        switch (pData[2])
        {
        case 1:
            notifCategory = "Cuoc goi den";
            incomingCall = true;
            Serial.println("Category: Incoming call");
            break;
        case 10:
            Serial.println("Category: Location");
        default:
            notifCategory = "Khong xac dinh";
            break;
        }
    }
    else if (pData[0] == 1)
    {
        Serial.println("Notification Modified!");
        if (pData[2] == 1)
        {
            Serial.println("Call Changed!");
        }
    }
    else if (pData[0] == 2)
    {
        Serial.println("Notification Removed!");
        if (pData[2] == 1)
        {
            Serial.println("Call Gone!");
            incomingCall = false;
            displayStandby();
        }
    }
    // Serial.println("pendingNotification");
    pendingNotification = true;
}

/**
 * Become a BLE client to a remote BLE server.  We are passed in the address of the BLE server
 * as the input parameter when the task is created.
 */
class MyClient : public Task
{
    void run(void *data)
    {

        BLEAddress *pAddress = (BLEAddress *)data;
        BLEClient *pClient = BLEDevice::createClient();
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_IO);
        pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        // Connect to the remove BLE Server.
        pClient->connect(*pAddress);

        /** BEGIN ANCS SERVICE **/
        // Obtain a reference to the service we are after in the remote BLE server.
        BLERemoteService *pAncsService = pClient->getService(ancsServiceUUID);
        if (pAncsService == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our service UUID: %s", ancsServiceUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic *pNotificationSourceCharacteristic = pAncsService->getCharacteristic(notificationSourceCharacteristicUUID);
        if (pNotificationSourceCharacteristic == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", notificationSourceCharacteristicUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic *pControlPointCharacteristic = pAncsService->getCharacteristic(controlPointCharacteristicUUID);
        if (pControlPointCharacteristic == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", controlPointCharacteristicUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic *pDataSourceCharacteristic = pAncsService->getCharacteristic(dataSourceCharacteristicUUID);
        if (pDataSourceCharacteristic == nullptr)
        {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", dataSourceCharacteristicUUID.toString().c_str());
            return;
        }
        const uint8_t v[] = {0x1, 0x0};
        pDataSourceCharacteristic->registerForNotify(dataSourceNotifyCallback);
        pDataSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)v, 2, true);
        pNotificationSourceCharacteristic->registerForNotify(NotificationSourceNotifyCallback);
        pNotificationSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)v, 2, true);
        /** END ANCS SERVICE **/

        // Hiển thị màn hình chờ
        displayStandby();

        while (1)
        {
            if (pendingNotification || incomingCall)
            {
                // CommandID: CommandIDGetNotificationAttributes
                // 32bit uid
                // AttributeID
                Serial.println("Requesting details...");
                const uint8_t vIdentifier[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x0};
                pControlPointCharacteristic->writeValue((uint8_t *)vIdentifier, 6, true);
                const uint8_t vTitle[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x1, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t *)vTitle, 8, true);
                const uint8_t vMessage[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x3, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t *)vMessage, 8, true);
                const uint8_t vDate[] = {0x0, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x5};
                pControlPointCharacteristic->writeValue((uint8_t *)vDate, 6, true);

                while (incomingCall)
                {
                    if (Serial.available() > 0)
                    {
                        acceptCall = Serial.read();
                        Serial.println((char)acceptCall);
                    }

                    if (acceptCall == 49)
                    { // call accepted , get number 1 from serial
                        const uint8_t vResponse[] = {0x02, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x00};
                        pControlPointCharacteristic->writeValue((uint8_t *)vResponse, 6, true);

                        acceptCall = 0;
                        // incomingCall = false;
                    }
                    else if (acceptCall == 48)
                    { // call rejected , get number 0 from serial
                        const uint8_t vResponse[] = {0x02, latestMessageID[0], latestMessageID[1], latestMessageID[2], latestMessageID[3], 0x01};
                        pControlPointCharacteristic->writeValue((uint8_t *)vResponse, 6, true);

                        acceptCall = 0;
                        incomingCall = false;
                        // Hiển thị màn hình chờ khi từ chối cuộc gọi
                        displayStandby();
                    }
                    delay(100);
                }

                pendingNotification = false;
            }
            delay(100); // does not work without small delay
        }
    } // run
}; // MyClient

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param)
    {
        clearDisplay();

        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 24, "Thiet bi da ket noi");
        u8g2.drawStr(0, 40, "Dang khoi tao ANCS...");
        u8g2.sendBuffer();

        MyClient *pMyClient = new MyClient();
        pMyClient->setStackSize(18000);
        pMyClient->start(new BLEAddress(param->connect.remote_bda));
    };

    void onDisconnect(BLEServer *pServer)
    {
        clearDisplay();
        
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 24, "Da ngat ket noi");
        u8g2.drawStr(0, 40, "Cho thiet bi moi...");
        u8g2.sendBuffer();
    }
};

class MainBLEServer : public Task
{
    void run(void *data)
    {
        ESP_LOGD(LOG_TAG, "Starting BLE work!");
        esp_log_buffer_char(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));
        esp_log_buffer_hex(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));

        // Initialize device
        BLEDevice::init("BLE Noti");
        BLEServer *pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        // Advertising parameters:
        // Soliciting ANCS
        BLEAdvertising *pAdvertising = pServer->getAdvertising();
        BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
        oAdvertisementData.setFlags(0x01);
        _setServiceSolicitation(&oAdvertisementData, BLEUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0"));
        pAdvertising->setAdvertisementData(oAdvertisementData);

        // Set security
        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_OUT);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

        // Start advertising
        pAdvertising->start();

        ESP_LOGD(LOG_TAG, "Advertising started!");
        delay(portMAX_DELAY);
    }

    /**
     * @brief Set the service solicitation (UUID)
     * @param [in] uuid The UUID to set with the service solicitation data.  Size of UUID will be used.
     */
    void _setServiceSolicitation(BLEAdvertisementData *a, BLEUUID uuid)
    {
        char cdata[2];
        switch (uuid.bitSize())
        {
        case 16:
        {
            // [Len] [0x14] [UUID16] data
            cdata[0] = 3;
            cdata[1] = ESP_BLE_AD_TYPE_SOL_SRV_UUID; // 0x14
            a->addData(std::string(cdata, 2) + std::string((char *)&uuid.getNative()->uuid.uuid16, 2));
            break;
        }

        case 128:
        {
            // [Len] [0x15] [UUID128] data
            cdata[0] = 17;
            cdata[1] = ESP_BLE_AD_TYPE_128SOL_SRV_UUID; // 0x15
            a->addData(std::string(cdata, 2) + std::string((char *)uuid.getNative()->uuid.uuid128, 16));
            break;
        }

        default:
            return;
        }
    } // setServiceSolicitationData
};

void SampleSecureServer(void)
{
    MainBLEServer *pMainBleServer = new MainBLEServer();
    pMainBleServer->setStackSize(20000);
    pMainBleServer->start();
}

void setup()
{
    Serial.begin(115200);

    // Khởi tạo màn hình OLED
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 24, "Dang khoi dong...");
    u8g2.sendBuffer();

    delay(1000);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 24, "Khoi tao BLE...");
    u8g2.drawStr(0, 40, "Cho ket noi iPhone");
    u8g2.sendBuffer();

    SampleSecureServer();
}

int b = 0;

void loop()
{
    displayBattery(b);
    if(b <100) {
     b++;
    }

    delay(1000);
}