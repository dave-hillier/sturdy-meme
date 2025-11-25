#include "CelestialCalculator.h"
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace {
    constexpr double PI = 3.14159265358979323846;
    constexpr double DEG_TO_RAD = PI / 180.0;
    constexpr double RAD_TO_DEG = 180.0 / PI;

    // J2000.0 epoch (January 1, 2000, 12:00 TT)
    constexpr double J2000 = 2451545.0;

    // Synodic month (new moon to new moon) in days
    constexpr double SYNODIC_MONTH = 29.530588853;

    // Known new moon reference (January 6, 2000, 18:14 UTC)
    constexpr double NEW_MOON_REFERENCE = 2451550.1;
}

// DateTime implementation

DateTime DateTime::fromTimeOfDay(float timeOfDay, int year, int month, int day) {
    DateTime dt;
    dt.year = year;
    dt.month = month;
    dt.day = day;

    float totalHours = timeOfDay * 24.0f;
    dt.hour = static_cast<int>(totalHours);
    float remainingMinutes = (totalHours - dt.hour) * 60.0f;
    dt.minute = static_cast<int>(remainingMinutes);
    dt.second = (remainingMinutes - dt.minute) * 60.0;

    return dt;
}

double DateTime::toJulianDay() const {
    // Algorithm from Jean Meeus' "Astronomical Algorithms"
    int y = year;
    int m = month;

    if (m <= 2) {
        y -= 1;
        m += 12;
    }

    int A = y / 100;
    int B = 2 - A + A / 4;

    double JD = std::floor(365.25 * (y + 4716))
              + std::floor(30.6001 * (m + 1))
              + day + B - 1524.5;

    // Add time of day
    JD += getFractionalHour() / 24.0;

    return JD;
}

double DateTime::getFractionalHour() const {
    return hour + minute / 60.0 + second / 3600.0;
}

// CelestialCalculator implementation

CelestialCalculator::CelestialCalculator()
    : location_(GeographicLocation::London()) {
}

void CelestialCalculator::setLocation(const GeographicLocation& location) {
    location_ = location;
}

double CelestialCalculator::normalizeAngle(double angle) {
    angle = std::fmod(angle, 360.0);
    if (angle < 0) angle += 360.0;
    return angle;
}

double CelestialCalculator::normalizeAngle180(double angle) {
    angle = std::fmod(angle + 180.0, 360.0);
    if (angle < 0) angle += 360.0;
    return angle - 180.0;
}

glm::vec3 CelestialCalculator::altAzToDirection(float altitude, float azimuth) {
    // Convert degrees to radians
    float altRad = altitude * static_cast<float>(DEG_TO_RAD);
    float azRad = azimuth * static_cast<float>(DEG_TO_RAD);

    // In our coordinate system:
    // Y is up
    // Z is North (azimuth 0)
    // X is East (azimuth 90)
    float y = std::sin(altRad);
    float horizontalDist = std::cos(altRad);
    float x = horizontalDist * std::sin(azRad);
    float z = horizontalDist * std::cos(azRad);

    return glm::normalize(glm::vec3(x, y, z));
}

void CelestialCalculator::calculateSolarParameters(double julianDay, double& declination, double& equationOfTime) const {
    // Days since J2000.0
    double n = julianDay - J2000;

    // Mean longitude of the Sun (degrees)
    double L = normalizeAngle(280.460 + 0.9856474 * n);

    // Mean anomaly of the Sun (degrees)
    double g = normalizeAngle(357.528 + 0.9856003 * n);
    double gRad = g * DEG_TO_RAD;

    // Ecliptic longitude of the Sun (degrees)
    double lambda = L + 1.915 * std::sin(gRad) + 0.020 * std::sin(2 * gRad);

    // Obliquity of the ecliptic (degrees) - simplified
    double epsilon = 23.439 - 0.0000004 * n;
    double epsilonRad = epsilon * DEG_TO_RAD;

    // Sun's declination
    double lambdaRad = lambda * DEG_TO_RAD;
    declination = std::asin(std::sin(epsilonRad) * std::sin(lambdaRad)) * RAD_TO_DEG;

    // Equation of time (in minutes)
    double y = std::tan(epsilonRad / 2);
    y *= y;
    double LRad = L * DEG_TO_RAD;
    equationOfTime = 4 * RAD_TO_DEG * (y * std::sin(2 * LRad)
                                       - 2 * 0.0167 * std::sin(gRad)
                                       + 4 * 0.0167 * y * std::sin(gRad) * std::cos(2 * LRad));
}

double CelestialCalculator::calculateLocalSiderealTime(double julianDay) const {
    // Days since J2000.0
    double D = julianDay - J2000;

    // Greenwich Mean Sidereal Time (in degrees)
    double GMST = normalizeAngle(280.46061837 + 360.98564736629 * D);

    // Local Sidereal Time
    double LST = GMST + location_.longitude;

    return normalizeAngle(LST);
}

void CelestialCalculator::equatorialToHorizontal(double rightAscension, double declination,
                                                   double localSiderealTime,
                                                   double& altitude, double& azimuth) const {
    // Hour angle
    double HA = localSiderealTime - rightAscension;
    HA = normalizeAngle180(HA);

    double HARad = HA * DEG_TO_RAD;
    double decRad = declination * DEG_TO_RAD;
    double latRad = location_.latitude * DEG_TO_RAD;

    // Calculate altitude
    double sinAlt = std::sin(decRad) * std::sin(latRad)
                  + std::cos(decRad) * std::cos(latRad) * std::cos(HARad);
    altitude = std::asin(sinAlt) * RAD_TO_DEG;

    // Calculate azimuth
    double cosAz = (std::sin(decRad) - std::sin(latRad) * sinAlt)
                 / (std::cos(latRad) * std::cos(altitude * DEG_TO_RAD));

    // Clamp to valid range for acos
    cosAz = std::max(-1.0, std::min(1.0, cosAz));
    azimuth = std::acos(cosAz) * RAD_TO_DEG;

    // Correct azimuth quadrant
    if (std::sin(HARad) > 0) {
        azimuth = 360.0 - azimuth;
    }
}

CelestialPosition CelestialCalculator::calculateSunPosition(const DateTime& dateTime) const {
    double julianDay = dateTime.toJulianDay();

    // Get solar parameters
    double declination, equationOfTime;
    calculateSolarParameters(julianDay, declination, equationOfTime);

    // Calculate local sidereal time
    double LST = calculateLocalSiderealTime(julianDay);

    // Sun's right ascension (simplified: assume sun is on ecliptic)
    // For better accuracy, we'd calculate this from ecliptic longitude
    double n = julianDay - J2000;
    double L = normalizeAngle(280.460 + 0.9856474 * n);
    double g = normalizeAngle(357.528 + 0.9856003 * n);
    double gRad = g * DEG_TO_RAD;
    double lambda = L + 1.915 * std::sin(gRad) + 0.020 * std::sin(2 * gRad);

    double epsilon = 23.439 - 0.0000004 * n;
    double epsilonRad = epsilon * DEG_TO_RAD;
    double lambdaRad = lambda * DEG_TO_RAD;

    // Right ascension
    double RA = std::atan2(std::cos(epsilonRad) * std::sin(lambdaRad), std::cos(lambdaRad)) * RAD_TO_DEG;
    RA = normalizeAngle(RA);

    // Convert to horizontal coordinates
    double altitude, azimuth;
    equatorialToHorizontal(RA, declination, LST, altitude, azimuth);

    CelestialPosition result;
    result.altitude = static_cast<float>(altitude);
    result.azimuth = static_cast<float>(azimuth);
    result.direction = altAzToDirection(result.altitude, result.azimuth);

    // Intensity based on altitude (smoothstep from -6 degrees to +10 degrees)
    // Civil twilight ends at -6 degrees
    float normalizedAlt = (result.altitude + 6.0f) / 16.0f;
    result.intensity = glm::clamp(normalizedAlt * normalizedAlt * (3.0f - 2.0f * normalizedAlt), 0.0f, 1.0f);

    return result;
}

void CelestialCalculator::calculateLunarParameters(double julianDay, double& rightAscension,
                                                    double& declination, double& phase) const {
    // Simplified lunar position calculation
    // Based on low-precision formulas from Meeus

    double T = (julianDay - J2000) / 36525.0;  // Julian centuries since J2000

    // Moon's mean longitude
    double L0 = normalizeAngle(218.3164477 + 481267.88123421 * T);

    // Moon's mean anomaly
    double M = normalizeAngle(134.9633964 + 477198.8675055 * T);
    double MRad = M * DEG_TO_RAD;

    // Moon's argument of latitude
    double F = normalizeAngle(93.2720950 + 483202.0175233 * T);
    double FRad = F * DEG_TO_RAD;

    // Sun's mean anomaly
    double Ms = normalizeAngle(357.5291092 + 35999.0502909 * T);
    double MsRad = Ms * DEG_TO_RAD;

    // Moon's mean elongation from sun
    double D = normalizeAngle(297.8501921 + 445267.1114034 * T);
    double DRad = D * DEG_TO_RAD;

    // Ecliptic longitude (simplified)
    double longitude = L0
        + 6.289 * std::sin(MRad)
        - 1.274 * std::sin(2 * DRad - MRad)
        + 0.658 * std::sin(2 * DRad)
        - 0.214 * std::sin(2 * MRad)
        - 0.186 * std::sin(MsRad);

    // Ecliptic latitude (simplified)
    double latitude = 5.128 * std::sin(FRad)
        + 0.281 * std::sin(MRad + FRad)
        - 0.278 * std::sin(FRad - MRad);

    longitude = normalizeAngle(longitude);

    // Convert ecliptic to equatorial
    double epsilon = 23.439 - 0.0000004 * (julianDay - J2000);
    double epsilonRad = epsilon * DEG_TO_RAD;
    double lonRad = longitude * DEG_TO_RAD;
    double latRad = latitude * DEG_TO_RAD;

    // Right ascension
    rightAscension = std::atan2(
        std::sin(lonRad) * std::cos(epsilonRad) - std::tan(latRad) * std::sin(epsilonRad),
        std::cos(lonRad)
    ) * RAD_TO_DEG;
    rightAscension = normalizeAngle(rightAscension);

    // Declination
    declination = std::asin(
        std::sin(latRad) * std::cos(epsilonRad)
        + std::cos(latRad) * std::sin(epsilonRad) * std::sin(lonRad)
    ) * RAD_TO_DEG;

    // Moon phase (0 = new, 0.5 = full, 1 = new again)
    double daysSinceNewMoon = std::fmod(julianDay - NEW_MOON_REFERENCE, SYNODIC_MONTH);
    if (daysSinceNewMoon < 0) daysSinceNewMoon += SYNODIC_MONTH;
    phase = daysSinceNewMoon / SYNODIC_MONTH;
}

MoonPosition CelestialCalculator::calculateMoonPosition(const DateTime& dateTime) const {
    double julianDay = dateTime.toJulianDay();

    // Get lunar parameters
    double rightAscension, declination, phase;
    calculateLunarParameters(julianDay, rightAscension, declination, phase);

    // Calculate local sidereal time
    double LST = calculateLocalSiderealTime(julianDay);

    // Convert to horizontal coordinates
    double altitude, azimuth;
    equatorialToHorizontal(rightAscension, declination, LST, altitude, azimuth);

    MoonPosition result;
    result.altitude = static_cast<float>(altitude);
    result.azimuth = static_cast<float>(azimuth);
    result.direction = altAzToDirection(result.altitude, result.azimuth);
    result.phase = static_cast<float>(phase);

    // Moon illumination (approximation based on phase)
    // At new moon (0), illumination = 0
    // At full moon (0.5), illumination = 1
    result.illumination = (1.0f - std::cos(result.phase * 2.0f * glm::pi<float>())) * 0.5f;

    // Intensity based on altitude and illumination
    float altFactor = glm::clamp((result.altitude + 5.0f) / 15.0f, 0.0f, 1.0f);
    result.intensity = altFactor * result.illumination * 0.3f;  // Moon is ~30% as bright as full sun

    return result;
}

glm::vec3 CelestialCalculator::getSunColor(float altitude) const {
    // Transition from orange/red at horizon to white at zenith
    float t = glm::smoothstep(-5.0f, 30.0f, altitude);

    glm::vec3 horizonColor(1.0f, 0.4f, 0.2f);   // Orange/red
    glm::vec3 zenithColor(1.0f, 0.98f, 0.95f);  // Warm white

    return glm::mix(horizonColor, zenithColor, t);
}

glm::vec3 CelestialCalculator::getAmbientColor(float sunAltitude) const {
    // Transition from night ambient to day ambient
    float t = glm::smoothstep(-10.0f, 10.0f, sunAltitude);

    glm::vec3 nightAmbient(0.02f, 0.02f, 0.05f);  // Dark blue
    glm::vec3 dayAmbient(0.15f, 0.15f, 0.2f);     // Light blue

    return glm::mix(nightAmbient, dayAmbient, t);
}
