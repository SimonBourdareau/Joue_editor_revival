#include "joue/printout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace joue {

namespace {

const double PointsPerMm = 72.0 / 25.4;

std::string number(double value) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.3f", value);
    std::string text(buffer);
    for (char& c : text) {
        if (c == ',') c = '.';
    }
    return text;
}

std::string points(double mm) { return number(mm * PointsPerMm); }

// A string for a PDF content stream: the characters of the standard fonts only, with the three that need a backslash.
std::string pdfText(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32 || c > 126) {
            out += '?';
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// The width of a text in Helvetica, near enough to centre it.
double textWidthMm(const std::string& text, double sizePt) {
    double width = 0;
    for (char c : text) width += (c == 'i' || c == 'l' || c == '.' || c == ',' || c == ' ' || c == '\'') ? 0.25 : (c == 'm' || c == 'w' || c == 'M' || c == 'W') ? 0.85 : 0.556;
    return width * sizePt / PointsPerMm;
}

class Page {
public:
    std::ostringstream stream;

    void line(double x0, double y0, double x1, double y1) { stream << points(x0) << ' ' << points(y0) << " m " << points(x1) << ' ' << points(y1) << " l S\n"; }
    void width(double pt) { stream << number(pt) << " w\n"; }
    void gray(double level) { stream << number(level) << " G " << number(level) << " g\n"; }
    void dash(double on, double off) { stream << '[' << points(on) << ' ' << points(off) << "] 0 d\n"; }
    void solid() { stream << "[] 0 d\n"; }

    // A rectangle with rounded corners, stroked; the corners are Bezier curves.
    void roundRect(double x, double y, double w, double h, double r) {
        r = std::min(r, std::min(w, h) / 2.0);
        const double k = 0.5522847498 * r;
        stream << points(x + r) << ' ' << points(y) << " m\n";
        stream << points(x + w - r) << ' ' << points(y) << " l\n";
        stream << points(x + w - r + k) << ' ' << points(y) << ' ' << points(x + w) << ' ' << points(y + r - k) << ' ' << points(x + w) << ' ' << points(y + r) << " c\n";
        stream << points(x + w) << ' ' << points(y + h - r) << " l\n";
        stream << points(x + w) << ' ' << points(y + h - r + k) << ' ' << points(x + w - r + k) << ' ' << points(y + h) << ' ' << points(x + w - r) << ' ' << points(y + h) << " c\n";
        stream << points(x + r) << ' ' << points(y + h) << " l\n";
        stream << points(x + r - k) << ' ' << points(y + h) << ' ' << points(x) << ' ' << points(y + h - r + k) << ' ' << points(x) << ' ' << points(y + h - r) << " c\n";
        stream << points(x) << ' ' << points(y + r) << " l\n";
        stream << points(x) << ' ' << points(y + r - k) << ' ' << points(x + r - k) << ' ' << points(y) << ' ' << points(x + r) << ' ' << points(y) << " c\n";
        stream << "h S\n";
    }

    void ellipse(double x, double y, double w, double h) {
        const double k = 0.5522847498;
        double cx = x + w / 2, cy = y + h / 2, rx = w / 2, ry = h / 2;
        stream << points(cx + rx) << ' ' << points(cy) << " m\n";
        stream << points(cx + rx) << ' ' << points(cy + k * ry) << ' ' << points(cx + k * rx) << ' ' << points(cy + ry) << ' ' << points(cx) << ' ' << points(cy + ry) << " c\n";
        stream << points(cx - k * rx) << ' ' << points(cy + ry) << ' ' << points(cx - rx) << ' ' << points(cy + k * ry) << ' ' << points(cx - rx) << ' ' << points(cy) << " c\n";
        stream << points(cx - rx) << ' ' << points(cy - k * ry) << ' ' << points(cx - k * rx) << ' ' << points(cy - ry) << ' ' << points(cx) << ' ' << points(cy - ry) << " c\n";
        stream << points(cx + k * rx) << ' ' << points(cy - ry) << ' ' << points(cx + rx) << ' ' << points(cy - k * ry) << ' ' << points(cx + rx) << ' ' << points(cy) << " c\n";
        stream << "h S\n";
    }

    void text(double x, double y, double sizePt, const std::string& content) {
        stream << "BT /F1 " << number(sizePt) << " Tf " << points(x) << ' ' << points(y) << " Td (" << pdfText(content) << ") Tj ET\n";
    }
    void centred(double centreX, double y, double sizePt, const std::string& content) { text(centreX - textWidthMm(content, sizePt) / 2.0, y, sizePt, content); }
};

}  // namespace

PrintPage printPage(const Design& design, const PrintOptions& options) {
    PrintPage page;
    page.widthMm = options.letter ? 279.4 : 297.0;
    page.heightMm = options.letter ? 215.9 : 210.0;
    double moduleWidth = static_cast<double>(design.widthMm());
    page.moduleLeftMm = (page.widthMm - moduleWidth) / 2.0;
    page.moduleBottomMm = (page.heightMm - Design::HeightMm) / 2.0 + 6.0;
    page.marginMm = page.moduleLeftMm;
    return page;
}

std::string designToPdf(const Design& design, const PrintOptions& options) {
    PrintPage layout = printPage(design, options);
    const double moduleWidth = static_cast<double>(design.widthMm()), moduleHeight = static_cast<double>(Design::HeightMm);
    const double left = layout.moduleLeftMm, bottom = layout.moduleBottomMm;
    Page page;

    if (options.grid) {
        page.gray(0.82);
        page.width(0.25);
        for (int x = 10; x < design.widthMm(); x += 10) {
            if (x % Design::SlotWidthMm == 0) continue;
            page.line(left + x, bottom, left + x, bottom + moduleHeight);
        }
        for (int y = 10; y < Design::HeightMm; y += 10) page.line(left, bottom + y, left + moduleWidth, bottom + y);
    }
    page.gray(0.0);
    page.width(0.8);
    page.roundRect(left, bottom, moduleWidth, moduleHeight, 4.5);
    if (design.slots() > 1) {
        page.gray(0.45);
        page.width(0.4);
        page.dash(2.0, 1.5);
        for (int x = Design::SlotWidthMm; x < design.widthMm(); x += Design::SlotWidthMm) page.line(left + x, bottom, left + x, bottom + moduleHeight);
        page.solid();
    }

    // the elements, the lower layers first, as the designer draws them
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < design.count(); ++i) {
        if (design.hasBox(i)) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return design.header(a).layer < design.header(b).layer; });
    page.gray(0.0);
    page.width(0.5);
    for (std::size_t i : order) {
        ElementHeader h = design.header(i);
        Box b = design.box(i);
        double x = left + b.x;
        double y = bottom + (options.mirror ? static_cast<double>(b.y) : moduleHeight - b.y - b.h);
        double w = b.w, hgt = b.h;
        switch (h.type) {
            case element::Round: page.ellipse(x, y, w, hgt); break;
            case element::Area:
                page.roundRect(x, y, w, hgt, 1.5);
                page.width(0.25);
                page.line(x + w / 2, y, x + w / 2, y + hgt);
                page.line(x, y + hgt / 2, x + w, y + hgt / 2);
                page.width(0.5);
                break;
            case element::Glide: break;
            default: page.roundRect(x, y, w, hgt, h.type == element::String ? 1.0 : 1.2); break;
        }
        bool labelled = h.type == element::Pad || h.type == element::Key || h.type == element::String;
        const std::vector<FieldSpec>& fields = bodyFields(h.type, h.version);
        if (options.labels && labelled && !fields.empty() && w >= 8 && hgt >= 5) {
            std::string label = noteName(design.field(i, fields[0]));
            page.centred(x + w / 2, y + hgt / 2 - 0.9, 6.5, label);
        }
    }

    if (options.marks) {
        // crop marks 2 mm outside each corner, and a scale bar that must measure exactly 100 mm when the sheet comes out of the printer
        page.gray(0.0);
        page.width(0.3);
        const double gap = 2.0, length = 6.0;
        for (int corner = 0; corner < 4; ++corner) {
            double cx = left + ((corner & 1) ? moduleWidth : 0), cy = bottom + ((corner & 2) ? moduleHeight : 0);
            double sx = (corner & 1) ? 1.0 : -1.0, sy = (corner & 2) ? 1.0 : -1.0;
            page.line(cx + sx * gap, cy, cx + sx * (gap + length), cy);
            page.line(cx, cy + sy * gap, cx, cy + sy * (gap + length));
        }
        const double barLeft = (layout.widthMm - 100.0) / 2.0, barY = bottom - 14.0;
        page.width(0.5);
        page.line(barLeft, barY, barLeft + 100.0, barY);
        for (int mm = 0; mm <= 100; mm += 10) page.line(barLeft + mm, barY, barLeft + mm, barY + ((mm % 50 == 0) ? 3.0 : 1.8));
        page.centred(barLeft, barY - 3.6, 6.5, "0");
        page.centred(barLeft + 50.0, barY - 3.6, 6.5, "50");
        page.centred(barLeft + 100.0, barY - 3.6, 6.5, "100 mm");
    }
    page.gray(0.0);
    std::string title = options.title.empty() ? std::string("Joue module layout") : options.title;
    page.text(left, bottom + moduleHeight + 17.0, 10.0, title);
    std::ostringstream info;
    info << design.slots() << (design.slots() == 1 ? " slot, " : " slots, ") << design.widthMm() << " x " << Design::HeightMm << " mm, " << (options.mirror ? "mirrored as in the designer" : "not mirrored") << ". Print at 100 % (actual size), not fit to page.";
    page.text(left, bottom + moduleHeight + 12.5, 7.0, info.str());

    // the file: five objects, then the table that says where each starts
    std::string content = page.stream.str();
    std::vector<std::string> objects(5);
    objects[0] = "<< /Type /Catalog /Pages 2 0 R >>";
    objects[1] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    objects[2] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + points(layout.widthMm) + ' ' + points(layout.heightMm) + "] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>";
    objects[3] = "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream";
    objects[4] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>";
    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    std::size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (std::size_t offset : offsets) {
        char entry[32];
        std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

}  // namespace joue
