#pragma once

#include <glm/glm.hpp>

// Geographic location on Earth
struct GeographicLocation {
    double latitude;   // Degrees, positive = North
    double longitude;  // Degrees, positive = East

    // Preset locations
    static GeographicLocation London() { return {51.5074, -0.1278}; }
    static GeographicLocation NewYork() { return {40.7128, -74.0060}; }
    static GeographicLocation Tokyo() { return {35.6762, 139.6503}; }
    static GeographicLocation Sydney() { return {-33.8688, 151.2093}; }
};

// Date and time representation
struct DateTime {
    int year;
    int month;      // 1-12
    int day;        // 1-31
    int hour;       // 0-23
    int minute;     // 0-59
    double second;  // 0-59.999...

    // Convert from time-of-day (0-1) to DateTime for a given date
    static DateTime fromTimeOfDay(float timeOfDay, int year = 2024, int month = 6, int day = 21);

    // Get Julian Day Number (astronomical time standard)
    double toJulianDay() const;

    // Get fractional hour (0-24)
    double getFractionalHour() const;
};

// Result of celestial body position calculation
struct CelestialPosition {
    glm::vec3 direction;    // Normalized direction vector in world space (Y-up)
    float altitude;         // Angle above horizon in degrees (-90 to +90)
    float azimuth;          // Angle from North, clockwise, in degrees (0-360)
    float intensity;        // Visibility factor (0 = below horizon, 1 = at zenith)
};

// Extended moon position with phase information
struct MoonPosition : public CelestialPosition {
    float phase;            // Moon phase (0 = new moon, 0.5 = full moon, 1 = new moon again)
    float illumination;     // Fraction of moon surface illuminated (0-1)
};

// Tidal information based on lunar/solar positions
struct TideInfo {
    float height;           // Current tide height relative to mean sea level (-1 to +1 normalized)
    float range;            // Current tidal range factor (0.5 = neap, 1.0 = spring)
    bool isRising;          // True if tide is currently rising
};

// Calculates astronomical positions of celestial bodies
class CelestialCalculator {
public:
    CelestialCalculator();

    // Set observer location on Earth
    void setLocation(const GeographicLocation& location);
    const GeographicLocation& getLocation() const { return location_; }

    // Calculate sun position for given date/time
    CelestialPosition calculateSunPosition(const DateTime& dateTime) const;

    // Calculate moon position for given date/time
    MoonPosition calculateMoonPosition(const DateTime& dateTime) const;

    // Calculate tidal state based on moon/sun positions
    // Uses simplified equilibrium tide model
    TideInfo calculateTide(const DateTime& dateTime) const;

    // Convenience: get sun color based on altitude (handles sunrise/sunset coloring)
    glm::vec3 getSunColor(float altitude) const;

    // Convenience: get ambient light based on sun altitude
    glm::vec3 getAmbientColor(float sunAltitude) const;

    // Convenience: get moon color based on altitude and illumination
    glm::vec3 getMoonColor(float moonAltitude, float illumination) const;

private:
    GeographicLocation location_;

    // Internal astronomical calculations

    // Calculate solar declination and equation of time
    void calculateSolarParameters(double julianDay, double& declination, double& equationOfTime) const;

    // Calculate lunar position parameters
    void calculateLunarParameters(double julianDay, double& rightAscension, double& declination, double& phase) const;

    // Convert altitude/azimuth to 3D direction vector (Y-up coordinate system)
    static glm::vec3 altAzToDirection(float altitude, float azimuth);

    // Convert right ascension/declination to altitude/azimuth for observer
    void equatorialToHorizontal(double rightAscension, double declination,
                                 double localSiderealTime,
                                 double& altitude, double& azimuth) const;

    // Calculate local sidereal time
    double calculateLocalSiderealTime(double julianDay) const;

    // Normalize angle to 0-360 range
    static double normalizeAngle(double angle);

    // Normalize angle to -180 to +180 range
    static double normalizeAngle180(double angle);
};
