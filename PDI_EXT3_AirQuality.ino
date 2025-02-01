///
/// @file PDI_EXT3_AirQuality.ino
/// @brief Example of air quality monitoring with e-paper display
///
/// @details Based on Sensirion mySN66 example
///
/// Tested on
/// - xG24 Explorer Kit
///
/// @author Rei Vilo
/// @date 21 Feb 2025
/// @version 903
///
/// @copyright (c) Rei Vilo, 2010-2025
/// @copyright Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)
/// @copyright For exclusive use with Pervasive Displays screens
///
/// @see
/// * https://www.researchgate.net/figure/IAir-Quality-Thresholds-for-PM25-PM10-and-CO2_fig2_342820337
/// * https://www.researchgate.net/figure/HUMIDEX-scores-by-air-temperature-o-C-and-relative-humidity-source_fig1_29462712
/// * Sensirion SN66 Sensing platform for PM, RH/T, VOC, Nox and CO2 measurements https://sensirion.com/products/catalog/SEN66
/// * PDLS_Basic Library for Pervasive Displays iTC screens, extension boards and development kits https://github.com/rei-vilo/PDLS_Basic
///

// SDK and configuration
#include "PDLS_Common.h"

// Board
const pins_t boardSiLabsBG24Explorer =
{
    .panelBusy = 4, ///< EXT3 and EXT3-1 pin 3 Red -> PC03 D4 4
    .panelDC = 5, ///< EXT3 and EXT3-1 pin 4 Orange -> PC06 D5 5
    .panelReset = 6, ///< EXT3 and EXT3-1 pin 5 Yellow -> PB00 D6 6
    .flashCS = NOT_CONNECTED, ///< EXT3 and EXT3-1 pin 8 Violet
    .panelCS = 13, ///< EXT3 and EXT3-1 pin 9 Grey -> PB03 A6 13
    .panelCSS = NOT_CONNECTED, ///< EXT4 not available
    .flashCSS = NOT_CONNECTED, ///< EXT3 pin 20 or EXT3-1 pin 11 Black2
    .touchInt = NOT_CONNECTED, ///< EXT4 not available
    .touchReset = NOT_CONNECTED, ///< EXT4 not available
    .panelPower = NOT_CONNECTED, ///< EXT4 pin 20 White -> PB04 A7 14
    .cardCS = NOT_CONNECTED, ///< External SD-card board
    .cardDetect = NOT_CONNECTED, ///< External SD-card board
};

pins_t myBoard = boardSiLabsBG24Explorer;

// Driver
#include "Pervasive_BWRY_Small.h"
Pervasive_BWRY_Small myDriver(eScreen_EPD_266_QS_0F, myBoard);

// Screen
#include "PDLS_Basic.h"
Screen_EPD myScreen(&myDriver);

// Variables
uint8_t fontSmall, fontMedium, fontLarge, fontVery;

#define DELAY_MINUTE 10
#define MATTER_EXAMPLE_NAME "EXT3 Air Quality"
#define MATTER_EXAMPLE_RELEASE 903

// SN66
#include "Wire.h"
#include "SensirionI2cSen66.h"

SensirionI2cSen66 mySN66;

// Ensure proper definition of NO_ERROR
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// Measure
struct measure_s
{
    float value = 0.0;
    float oldValue = 0.0;
    uint8_t level = 0;
    String name = "";
    String unitISO = "";
    String unitUTF = "";
    int8_t trend = 0;
};

measure_s pm1p0;
measure_s pm2p5;
measure_s pm4p0;
measure_s pm10p0;
measure_s temperature;
measure_s humidity;
measure_s humidex;
measure_s voc;
measure_s nox;
measure_s co2;

static uint32_t chrono32 = 0;
static uint8_t countFlush = 1; // Counter for global update
static uint8_t minutes = 0;
const uint8_t FAST_BEFORE_NORMAL = 8; // Number of fast updates before normal update
bool flagDisplay = true;
uint8_t result;

// Levels
const uint16_t frontColours[5] = { myColours.black, myColours.red, myColours.black, myColours.white, myColours.white };
const uint16_t backColours[5] = { myColours.white, myColours.white, myColours.yellow, myColours.red, myColours.black };
const char * stringLevel[] = {"Good", "Moderate", "Unhealthy for sensitive groups", "Unhealthy", "Very Unhealthy or Hazardous"};
const char * stringTrend[] = {"-", "=", "+"};

// Prototypes

// Functions

// --- SN66
///
/// @brief Configure mySN66 SN66
///
/// @return false RESULT_SUCCESS
/// @return true RESULT_ERROR
///
bool configureSensor()
{
    static char errorMessage[64];
    static int16_t error;

    Wire1.begin(); // Qwiic connected to Wire1 on BG24
    mySN66.begin(Wire1, SEN66_I2C_ADDR_6B);

    error = mySN66.deviceReset();
    if (error != NO_ERROR)
    {
        errorToString(error, errorMessage, sizeof errorMessage);
        hV_HAL_log(LEVEL_INFO, "SN60 reset - '%s'", errorMessage);
        return RESULT_ERROR;
    }
    delay(1200);

    uint8_t serialNumber[32] = {0};
    error = mySN66.getSerialNumber(serialNumber, 32);
    if (error != NO_ERROR)
    {
        errorToString(error, errorMessage, sizeof errorMessage);
        hV_HAL_log(LEVEL_INFO, "SN60 serial number - '%s'", errorMessage);
        return RESULT_ERROR;
    }
    hV_HAL_log(LEVEL_INFO, "SN60 serial number - '%s'", serialNumber);

    error = mySN66.startContinuousMeasurement();
    if (error != NO_ERROR)
    {
        errorToString(error, errorMessage, sizeof errorMessage);
        hV_HAL_log(LEVEL_INFO, "SN60 start - '%s'", errorMessage);
        return RESULT_ERROR;
    }

    return RESULT_SUCCESS;
}

///
/// @brief Read values from SN66
///
/// @return false RESULT_SUCCESS
/// @return true RESULT_ERROR
///
bool readSensor()
{
    static char errorMessage[64];
    static int16_t error;

    uint16_t ui16co2 = 0;

    error = mySN66.readMeasuredValues(
                pm1p0.value, pm2p5.value, pm4p0.value, pm10p0.value,
                humidity.value, temperature.value,
                voc.value, nox.value,
                ui16co2);

    co2.value = (float)ui16co2;

    if (error != NO_ERROR)
    {
        // Serial.print("Error trying to execute readMeasuredValues():");
        errorToString(error, errorMessage, sizeof errorMessage);
        // Serial.println(errorMessage);
        hV_HAL_log(LEVEL_INFO, "SN60 read - '%s'", errorMessage);
        return RESULT_ERROR;
    }

    // From https://ptaff.ca/humidex/?lang=en_CA
    float e = humidity.value / 100.0 * 6.112 * exp((17.67 * temperature.value) / (temperature.value + 243.5));
    humidex.value = temperature.value + (5.0 / 9.0) * (e - 10.0);

    return RESULT_SUCCESS;
}

// Measures
///
/// @brief Calculate the level and trend
///
/// @param measure to be checked
/// @param level1 threshold for first level, red on white
/// @param level1 threshold for second level, black on yellow
/// @param level2 threshold for third level, black on red
/// @param level3 threshold for fourth level, white on black
///
/// @note Default level is zero, black on white
/// @note 0 < level1 < level2 < level3
///
void calculateLevelTrend(measure_s & measure, float level1, float level2, float level3, float level4)
{
    // Level
    if (measure.value < level1)
    {
        measure.level = 0; // 0 (Good)
    }
    else if (measure.value < level2)
    {
        measure.level = 1; // 1 Moderate
    }
    else if (measure.value < level3)
    {
        measure.level = 2; // 2 Unhealthy for sensitive groups
    }
    else if (measure.value < level4)
    {
        measure.level = 3; // 3 Unhealthy
    }
    else
    {
        measure.level = 4; // 4 Very Unhealthy
    }

    // Trend
    if (measure.value > measure.oldValue * 1.02)
    {
        measure.trend = 1; // More than 2% increase
    }
    else if (measure.value < measure.oldValue * 0.98)
    {
        measure.trend = -1; // More than 2% decrease
    }
    else
    {
        measure.trend = 0; // Within +/-2%
    }

    measure.oldValue = measure.value;
}

// E-paper display
///
/// @brief Display measure
///
/// @param column 0, 1 or 2
/// @param row depdending of the column
/// @param measure
///
void displaySection(uint8_t column, uint8_t row, measure_s measure)
{
    uint16_t x, y, dx, dy, tx, ty, tz;

    x = myScreen.screenSizeX();
    y = myScreen.screenSizeY();
    dx = x / 3;

    // Checks
    if (column > 2)
    {
        return;
    }
    if (measure.level > 3)
    {
        return;
    }

    uint16_t frontColour = frontColours[measure.level];
    uint16_t backColour = backColours[measure.level];

    uint8_t segments[3] = { 4, 3, 3 };
    dy = y / segments[column];

    tx = dx * column;
    ty = dy * row;

    myScreen.setPenSolid(true);
    myScreen.dRectangle(tx, ty, dx - 1, dy - 1, backColour);

    // Name
    myScreen.selectFont(fontSmall);
    myScreen.gText(tx + 2, ty + 2, measure.name, frontColour, backColour);
    tz = myScreen.characterSizeY();

    // Unit
    myScreen.selectFont(fontMedium);
    tx = dx * column + dx - myScreen.stringSizeX(measure.unitISO);
    ty = dy * row;
    myScreen.gText(tx - 4, ty + 2, measure.unitISO, frontColour, backColour);
    tz = max(tz, myScreen.characterSizeY());

    // Value
    myScreen.selectFont(fontLarge);
    tx = dx * column + (dx - myScreen.stringSizeX(formatString("%5.1f", measure.value))) / 2;
    ty = dy * row + tz + (dy - tz - myScreen.characterSizeY()) / 2;
    myScreen.gText(tx, ty, formatString("%5.1f", measure.value), frontColour, backColour);

    // Trend
    myScreen.selectFont(fontMedium);
    tx = dx * column;
    ty = dy * row + myScreen.characterSizeY();
    myScreen.gText(tx + 2, ty + 2, stringTrend[measure.trend + 1], frontColour, backColour);

    // Frame
    // tx = dx * column;
    // ty = dy * row;
    // myScreen.setPenSolid(false);
    // myScreen.dRectangle(tx, ty, dx, dy, frontColour);
}

///
/// @brief About page
///
void displayAbout()
{
    myScreen.clear();

    uint16_t x = 0;
    uint16_t y = 0;

    myScreen.selectFont(fontLarge);
    myScreen.gText(0, 0, "Air Quality Monitoring");
    myScreen.gText(1, 0, "Air Quality Monitoring");
    y = myScreen.characterSizeY();

    uint16_t dy = (myScreen.screenSizeY() - y) / 5;
    uint16_t dx = dy * 4 / 3;

    myScreen.selectFont(fontMedium);
    uint16_t tx = (dy - myScreen.characterSizeX()) / 2;
    uint16_t ty = (dy - myScreen.characterSizeY()) / 2;

    for (uint8_t level = 0; level < 5; level += 1)
    {
        uint16_t frontColour = frontColours[level];
        uint16_t backColour = backColours[level];

        myScreen.setPenSolid(true);
        myScreen.dRectangle(x, y + dy * level, dy, dy - 1, backColour);
        myScreen.gText(x + tx, y + dy * level + ty, formatString("%i", level), frontColour, backColour);
        myScreen.setPenSolid(false);
        myScreen.dRectangle(x + 2, y + dy * level + 2, dy - 4, dy - 5, frontColour);

        myScreen.gText(x + dx, y + dy * level + ty, stringLevel[level], myColours.black);
    }

    myScreen.flush();
}

///
/// @brief Print measure on Serial console
///
/// @param measure
///
void printMeasure(measure_s measure)
{
    hV_HAL_log(LEVEL_INFO, "%12s %8.1f %-6s L%i %s", measure.name.c_str(), measure.value, measure.unitUTF.c_str(), measure.level, stringTrend[measure.trend + 1]);
}

///
/// @brief Setup
///
void setup()
{
    hV_HAL_begin();
    // mySerial = Serial by default, otherwise edit hV_HAL_Peripherals.h
    hV_HAL_Serial_crlf();
    hV_HAL_log(LEVEL_INFO, "%s", __FILE__);
    hV_HAL_log(LEVEL_INFO, "%s %s", __DATE__, __TIME__);
    hV_HAL_Serial_crlf();

    // Start
    myScreen.begin();
    myScreen.setOrientation(ORIENTATION_LANDSCAPE);

    // Fonts
    fontSmall = Font_Terminal6x8;
    fontMedium = Font_Terminal8x12;
    fontLarge = Font_Terminal12x16;
    fontVery = Font_Terminal16x24;

    // --- SN66
    result = configureSensor();
    // --- End of SN66

    // Buttons
    hV_HAL_GPIO_define(BTN_BUILTIN, INPUT_PULLUP); // Exit
    hV_HAL_GPIO_define(BTN_BUILTIN_1, INPUT_PULLUP); // About
    // minutes = DELAY_MINUTE - 1;

    // Measures
    pm1p0.name = "PM1.0";
    pm1p0.unitUTF = "ug/m3";
    pm1p0.unitISO = utf2iso("µg/m³");

    pm2p5.name = "PM2.5";
    pm2p5.unitUTF = "ug/m3";
    pm2p5.unitISO = utf2iso("µg/m³");

    pm4p0.name = "PM4.0";
    pm4p0.unitUTF = "ug/m3";
    pm4p0.unitISO = utf2iso("µg/m³");

    pm10p0.name = "PM10.";
    pm10p0.unitUTF = "ug/m3";
    pm10p0.unitISO = utf2iso("µg/m³");

    temperature.name = "Temperature";
    temperature.unitUTF = "oC";
    temperature.unitISO = utf2iso("°C");

    humidity.name = "Humidity";
    humidity.unitUTF = "%";
    humidity.unitISO = utf2iso(humidity.unitUTF);

    humidex.name = "Humidex";
    humidex.unitUTF = "oC";
    humidex.unitISO = utf2iso("°C");

    voc.name = "VOC";
    voc.unitUTF = "index";
    voc.unitISO = utf2iso(voc.unitUTF);

    nox.name = "NOx";
    nox.unitUTF = "index";
    nox.unitISO = utf2iso(nox.unitUTF);

    co2.name = "CO2";
    co2.unitUTF = "ppm";
    co2.unitISO = utf2iso(co2.unitUTF);
}

///
/// @brief Loop
///
void loop()
{
    if (hV_HAL_GPIO_read(BTN_BUILTIN) == LOW)
    {
        myScreen.regenerate();
        hV_HAL_exit();
    }

    if (hV_HAL_GPIO_read(BTN_BUILTIN_1) == LOW)
    {
        displayAbout();
        // minutes = 0;
    }

    if (hV_HAL_getMilliseconds() > chrono32)
    {
        minutes += 1;
        minutes %= DELAY_MINUTE;
        hV_HAL_log(LEVEL_INFO, "minutes = %i", minutes);

        // SN66
        result = readSensor();
        calculateLevelTrend(pm1p0, 36.0, 76.0, 151.0, 251.0);
        calculateLevelTrend(pm2p5, 13.0, 36.0, 56.0, 151.0);
        calculateLevelTrend(pm4p0, 26.0, 51.0, 101.0, 151.0);
        calculateLevelTrend(pm10p0, 51.0, 101.0, 151.0, 251.0);

        calculateLevelTrend(temperature, 20, 30, 40, 50);
        calculateLevelTrend(humidity, 40, 50, 60, 70);
        calculateLevelTrend(humidex, 30, 40, 46, 55);

        calculateLevelTrend(voc, 100, 200, 300, 400);
        calculateLevelTrend(nox, 100, 200, 300, 400);
        calculateLevelTrend(co2, 501.0, 1001.0, 1501.0, 2001.0);

        // Serial
        if (result == RESULT_SUCCESS)
        {
            printMeasure(pm1p0);
            printMeasure(pm2p5);
            printMeasure(pm4p0);
            printMeasure(pm10p0);

            printMeasure(temperature);
            printMeasure(humidity);
            printMeasure(humidex);

            printMeasure(voc);
            printMeasure(nox);
            printMeasure(co2);

            hV_HAL_Serial_crlf();
        }
        else
        {
            hV_HAL_log(LEVEL_INFO, "Error");
            hV_HAL_Serial_crlf();
        }
        // chrono32 = millis() + 3600000; // 1 minute
        chrono32 = millis() + 10000; // 10 seconds
    }

    if (minutes == 0)
    {
        if (result == RESULT_SUCCESS)
        {
            myScreen.setPenSolid(false);
            myScreen.selectFont(fontMedium);

            myScreen.clear();
            displaySection(0, 0, pm1p0);
            displaySection(0, 1, pm2p5);
            displaySection(0, 2, pm4p0);
            displaySection(0, 3, pm10p0);

            displaySection(1, 0, temperature);
            displaySection(1, 1, humidity);
            displaySection(1, 2, humidex);

            displaySection(2, 0, voc);
            displaySection(2, 1, nox);
            displaySection(2, 2, co2);

            myScreen.flush();
        }
    }

    hV_HAL_delayMilliseconds(100);
}

