/**
 * SVGWriter.hpp - SVG document builder
 *
 * This is a new utility class for building SVG documents. It provides
 * methods to create SVG elements that correspond to the OpenFL Graphics
 * API calls used in CityMap.hx.
 */
#pragma once

#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

namespace town {

class SVGWriter {
private:
    std::ostringstream buffer;
    int indentLevel = 0;

    std::string indent() const {
        return std::string(indentLevel * 2, ' ');
    }

    // Format float with reasonable precision, trimming trailing zeros
    static std::string formatFloat(float v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << v;
        std::string s = oss.str();
        // Trim trailing zeros after decimal point
        size_t dot = s.find('.');
        if (dot != std::string::npos) {
            size_t last = s.find_last_not_of('0');
            if (last != std::string::npos && last > dot) {
                s = s.substr(0, last + 1);
            } else if (last == dot) {
                s = s.substr(0, dot);
            }
        }
        return s;
    }

    // Escape special XML characters
    static std::string escapeXml(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '&': result += "&amp;"; break;
                case '"': result += "&quot;"; break;
                case '\'': result += "&apos;"; break;
                default: result += c;
            }
        }
        return result;
    }

public:
    SVGWriter() = default;

    // Begin SVG document with dimensions and optional viewBox
    void beginDocument(float width, float height,
                       float viewBoxMinX = 0, float viewBoxMinY = 0,
                       float viewBoxWidth = 0, float viewBoxHeight = 0) {
        buffer << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        buffer << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
        buffer << "width=\"" << formatFloat(width) << "\" ";
        buffer << "height=\"" << formatFloat(height) << "\"";

        if (viewBoxWidth > 0 && viewBoxHeight > 0) {
            buffer << " viewBox=\"" << formatFloat(viewBoxMinX) << " "
                   << formatFloat(viewBoxMinY) << " "
                   << formatFloat(viewBoxWidth) << " "
                   << formatFloat(viewBoxHeight) << "\"";
        }
        buffer << ">\n";
        indentLevel++;
    }

    void endDocument() {
        indentLevel--;
        buffer << "</svg>\n";
    }

    // Begin a group element
    void beginGroup(const std::string& id = "", const std::string& className = "") {
        buffer << indent() << "<g";
        if (!id.empty()) {
            buffer << " id=\"" << escapeXml(id) << "\"";
        }
        if (!className.empty()) {
            buffer << " class=\"" << escapeXml(className) << "\"";
        }
        buffer << ">\n";
        indentLevel++;
    }

    void endGroup() {
        indentLevel--;
        buffer << indent() << "</g>\n";
    }

    // Draw a rectangle (used for background)
    void rect(float x, float y, float width, float height,
              const std::string& fill, const std::string& stroke = "none",
              float strokeWidth = 0) {
        buffer << indent() << "<rect";
        buffer << " x=\"" << formatFloat(x) << "\"";
        buffer << " y=\"" << formatFloat(y) << "\"";
        buffer << " width=\"" << formatFloat(width) << "\"";
        buffer << " height=\"" << formatFloat(height) << "\"";
        buffer << " fill=\"" << fill << "\"";
        if (stroke != "none" && strokeWidth > 0) {
            buffer << " stroke=\"" << stroke << "\"";
            buffer << " stroke-width=\"" << formatFloat(strokeWidth) << "\"";
        }
        buffer << "/>\n";
    }

    // Draw a polygon (closed path)
    void polygon(const Polygon& poly, const std::string& fill,
                 const std::string& stroke = "none", float strokeWidth = 0,
                 const std::string& strokeLinejoin = "miter") {
        if (poly.size() < 3) return;

        buffer << indent() << "<polygon points=\"";
        for (size_t i = 0; i < poly.size(); i++) {
            if (i > 0) buffer << " ";
            buffer << formatFloat(poly[i]->x) << "," << formatFloat(poly[i]->y);
        }
        buffer << "\" fill=\"" << fill << "\"";

        if (stroke != "none" && strokeWidth > 0) {
            buffer << " stroke=\"" << stroke << "\"";
            buffer << " stroke-width=\"" << formatFloat(strokeWidth) << "\"";
            buffer << " stroke-linejoin=\"" << strokeLinejoin << "\"";
        }
        buffer << "/>\n";
    }

    // Draw a polyline (open path)
    void polyline(const Polygon& poly, const std::string& stroke,
                  float strokeWidth, const std::string& strokeLinecap = "butt") {
        if (poly.size() < 2) return;

        buffer << indent() << "<polyline points=\"";
        for (size_t i = 0; i < poly.size(); i++) {
            if (i > 0) buffer << " ";
            buffer << formatFloat(poly[i]->x) << "," << formatFloat(poly[i]->y);
        }
        buffer << "\" fill=\"none\"";
        buffer << " stroke=\"" << stroke << "\"";
        buffer << " stroke-width=\"" << formatFloat(strokeWidth) << "\"";
        buffer << " stroke-linecap=\"" << strokeLinecap << "\"";
        buffer << "/>\n";
    }

    // Draw a circle
    void circle(float cx, float cy, float r,
                const std::string& fill, const std::string& stroke = "none",
                float strokeWidth = 0) {
        buffer << indent() << "<circle";
        buffer << " cx=\"" << formatFloat(cx) << "\"";
        buffer << " cy=\"" << formatFloat(cy) << "\"";
        buffer << " r=\"" << formatFloat(r) << "\"";
        buffer << " fill=\"" << fill << "\"";
        if (stroke != "none" && strokeWidth > 0) {
            buffer << " stroke=\"" << stroke << "\"";
            buffer << " stroke-width=\"" << formatFloat(strokeWidth) << "\"";
        }
        buffer << "/>\n";
    }

    // Draw a line
    void line(float x1, float y1, float x2, float y2,
              const std::string& stroke, float strokeWidth,
              const std::string& strokeLinecap = "butt") {
        buffer << indent() << "<line";
        buffer << " x1=\"" << formatFloat(x1) << "\"";
        buffer << " y1=\"" << formatFloat(y1) << "\"";
        buffer << " x2=\"" << formatFloat(x2) << "\"";
        buffer << " y2=\"" << formatFloat(y2) << "\"";
        buffer << " stroke=\"" << stroke << "\"";
        buffer << " stroke-width=\"" << formatFloat(strokeWidth) << "\"";
        buffer << " stroke-linecap=\"" << strokeLinecap << "\"";
        buffer << "/>\n";
    }

    // Add a comment
    void comment(const std::string& text) {
        buffer << indent() << "<!-- " << escapeXml(text) << " -->\n";
    }

    // Get the complete SVG string
    std::string toString() const {
        return buffer.str();
    }

    // Clear the buffer
    void clear() {
        buffer.str("");
        buffer.clear();
        indentLevel = 0;
    }
};

} // namespace town
