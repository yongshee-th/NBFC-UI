#include <QtWidgets/QApplication>
#include <QtUiTools/QUiLoader>
#include <QtCore/QFile>
#include <QtWidgets/QWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QAction>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGraphicsDropShadowEffect>
#include <QtWidgets/QGraphicsOpacityEffect>
#include <QtCore/QProcess>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QDir>
#include <QtCore/QVariantAnimation>
#include <QtCore/QEasingCurve>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QLinearGradient>
#include <QtGui/QConicalGradient>
#include <QtGui/QFont>
#include <algorithm>
#include <unistd.h>

const QString APP_VERSION = "2.4";

// หาไฟล์อุณหภูมิ CPU Package จาก hwmon "coretemp" ตามชื่อ (เลข hwmonN เปลี่ยนได้ทุกครั้งที่บูต)
QString findCpuPackageSensor() {
    const auto dirs = QDir("/sys/class/hwmon").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &d : dirs) {
        QString base = "/sys/class/hwmon/" + d;
        QFile n(base + "/name");
        if (!n.open(QIODevice::ReadOnly) || QString(n.readAll()).trimmed() != "coretemp") continue;
        for (int i = 1; i <= 4; ++i) {
            QFile l(QString("%1/temp%2_label").arg(base).arg(i));
            if (l.open(QIODevice::ReadOnly) && QString(l.readAll()).startsWith("Package"))
                return QString("%1/temp%2_input").arg(base).arg(i);
        }
    }
    return {};
}

struct Config {
    QString nbfcPath = "nbfc";
    int updateIntervalMs = 2000;
    QString cpuSensorPath = "/sys/class/thermal/thermal_zone0/temp";
    QString gpuSensorPath = "/sys/class/thermal/thermal_zone1/temp";
    bool autoDetectSensors = true;
    QString currentTheme = "dark";
    QJsonObject themes;

    void load() {
        QString configPath = QCoreApplication::applicationDirPath() + "/Config.json";
        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Could not open Config.json, using defaults.";
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject root = doc.object();

        if (root.contains("settings")) {
            QJsonObject settings = root["settings"].toObject();
            nbfcPath = settings.value("nbfc_path").toString(nbfcPath);
            updateIntervalMs = settings.value("update_interval_ms").toInt(updateIntervalMs);
            cpuSensorPath = settings.value("cpu_sensor_path").toString(cpuSensorPath);
            gpuSensorPath = settings.value("gpu_sensor_path").toString(gpuSensorPath);
            autoDetectSensors = settings.value("auto_detect_sensors").toBool(autoDetectSensors);
        }
        currentTheme = root.value("current_theme").toString(currentTheme);
        themes = root.value("themes").toObject();
        file.close();

        // ค่าเดิม (acpitz thermal_zone0) ไม่ใช่อุณหภูมิ CPU จริง และเฉลี่ยคอร์ทำให้ต่ำกว่าจริง -> ใช้ Package แทน
        if (cpuSensorPath.isEmpty() || cpuSensorPath == "auto"
            || cpuSensorPath == "/sys/class/thermal/thermal_zone0/temp") {
            QString pkg = findCpuPackageSensor();
            if (!pkg.isEmpty()) cpuSensorPath = pkg;
        }
    }

    void save() {
        QString configPath = QCoreApplication::applicationDirPath() + "/Config.json";
        QFile file(configPath);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Could not open Config.json for writing.";
            return;
        }
        QJsonObject root;
        QJsonObject settings;
        settings["version"] = APP_VERSION;
        settings["nbfc_path"] = nbfcPath;
        settings["update_interval_ms"] = updateIntervalMs;
        settings["cpu_sensor_path"] = cpuSensorPath;
        settings["gpu_sensor_path"] = gpuSensorPath;
        settings["auto_detect_sensors"] = autoDetectSensors;

        root["settings"] = settings;
        root["current_theme"] = currentTheme;
        root["themes"] = themes;

        file.write(QJsonDocument(root).toJson());
        file.close();
    }
};

Config g_config;
QJsonObject g_activeTheme; // ธีมที่ใช้งานอยู่ตอนนี้ (แคชไว้ให้ widget อื่นดึงสีไปใช้ เช่น mode pill / status dot)

// --- รายชื่อ thermal zone ทั้งหมดในเครื่อง พร้อมชื่อชนิดและอุณหภูมิปัจจุบัน
// ใช้ให้ผู้ใช้เลือก sensor path ที่ถูกต้องแทนการเดา/พิมพ์ path เอง ---
struct ThermalZoneInfo { QString path; QString label; };

QList<ThermalZoneInfo> listThermalZones() {
    QList<ThermalZoneInfo> zones;
    QDir dir("/sys/class/thermal");
    QStringList entries = dir.entryList(QStringList() << "thermal_zone*", QDir::Dirs, QDir::Name);
    for (const QString &entry : entries) {
        QString base = "/sys/class/thermal/" + entry;

        QString type = "?";
        QFile typeFile(base + "/type");
        if (typeFile.open(QIODevice::ReadOnly)) { type = QString(typeFile.readAll()).trimmed(); typeFile.close(); }

        QString tempStr = "N/A";
        QFile tempFile(base + "/temp");
        if (tempFile.open(QIODevice::ReadOnly)) {
            double t = QString(tempFile.readAll()).trimmed().toDouble() / 1000.0;
            tempFile.close();
            if (t > 0 && t < 150) tempStr = QString::number(t, 'f', 1) + "°C";
        }

        zones.append({base + "/temp", QString("%1 (%2) — %3").arg(entry, type, tempStr)});
    }
    return zones;
}

// --- เกจวงกลมแบบสปีดมิเตอร์ แสดงรอบพัดลม (ตัวเลขกลาง) และเปอร์เซ็นต์เป้าหมาย (ส่วนโค้งที่เติมสี)
// ค่าจะเคลื่อนไหวลื่นๆ ทุกครั้งที่อัปเดต แทนที่จะกระโดดเปลี่ยนทันที ---
class FanGauge : public QWidget {
public:
    explicit FanGauge(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(150, 150);
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(500);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_displayPercent = v.toDouble();
            update();
        });

        // ใบพัดหมุนจริงตามรอบพัดลม (0 RPM = ใบพัดหยุดนิ่ง สื่อสถานะจริง)
        m_spinTimer = new QTimer(this);
        // ~25fps พอให้ดูลื่น และหยุดตัวจับเวลาเมื่อพัดลมไม่หมุน/หน้าต่างถูกซ่อน เพื่อไม่กิน CPU ตอนว่าง
        m_spinTimer->setInterval(40);
        QObject::connect(m_spinTimer, &QTimer::timeout, this, [this]() {
            m_angle += m_angularVelocity * (m_spinTimer->interval() / 1000.0);
            if (m_angle > 360.0) m_angle -= 360.0;
            update();
        });
    }

    void showEvent(QShowEvent *e) override {
        QWidget::showEvent(e);
        syncSpinTimer();
    }
    void hideEvent(QHideEvent *e) override {
        QWidget::hideEvent(e);
        syncSpinTimer();
    }

    void setColors(const QColor &accent, const QColor &accent2, const QColor &track,
                    const QColor &text, const QColor &textMuted) {
        m_accent = accent; m_accent2 = accent2; m_track = track;
        m_text = text; m_textMuted = textMuted;
        update();
    }

    // percent: เติมส่วนโค้ง 0-100 (มาจาก Target Fan Speed) / rpmValue: ตัวเลขใหญ่กลางเกจ (รอบพัดลมจริง)
    void setValues(int percent, int rpmValue, const QString &captionText) {
        percent = qBound(0, percent, 100);
        m_rpm = rpmValue;
        m_caption = captionText;
        m_anim->stop();
        m_anim->setStartValue(m_displayPercent);
        m_anim->setEndValue(static_cast<double>(percent));
        m_anim->start();

        // ความเร็วหมุนของใบพัดบนจอ ไม่ใช่ 1:1 กับ RPM จริง (จะไวเกินจนมองไม่ออกว่าหมุน)
        // แต่ปรับตามสัดส่วนของ RPM สูงสุดที่พบเจอจริง ให้ยิ่งหมุนเร็ว = รอบสูงจริง
        m_seenMaxRpm = qMax(m_seenMaxRpm, qMax(rpmValue, 1));
        double norm = qBound(0.0, rpmValue / double(m_seenMaxRpm), 1.0);
        m_angularVelocity = norm * 360.0; // สูงสุดประมาณ 1 รอบ/วินาที ให้ดูลื่นตายังพอมองตามได้
        syncSpinTimer();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int side = qMin(width(), height());
        QRectF rect((width() - side) / 2.0 + 12, (height() - side) / 2.0 + 12, side - 24, side - 24);

        const qreal startAngle = 225.0;
        const qreal fullSweep = -270.0;

        QPen trackPen(m_track, 10, Qt::SolidLine, Qt::RoundCap);
        p.setPen(trackPen);
        p.drawArc(rect, static_cast<int>(startAngle * 16), static_cast<int>(fullSweep * 16));

        qreal sweepValue = fullSweep * (m_displayPercent / 100.0);
        QConicalGradient grad(rect.center(), startAngle);
        grad.setColorAt(0.0, m_accent);
        grad.setColorAt(0.75, m_accent2);
        grad.setColorAt(1.0, m_accent2);
        QPen valuePen(QBrush(grad), 10, Qt::SolidLine, Qt::RoundCap);
        p.setPen(valuePen);
        p.drawArc(rect, static_cast<int>(startAngle * 16), static_cast<int>(sweepValue * 16));

        // ใบพัดหมุนจางๆ อยู่หลังตัวเลข เป็นตัวชี้วัดภาพว่าพัดลม "กำลังหมุนอยู่จริง" แค่ไหน
        QRectF bladeArea(rect.center().x() - side * 0.30, rect.center().y() - side * 0.30, side * 0.60, side * 0.60);
        drawBlades(p, bladeArea, m_angle, m_accent);

        p.setPen(m_text);
        QFont f = font();
        f.setPointSize(qMax(16, side / 8));
        f.setBold(true);
        p.setFont(f);
        QRectF numRect(rect.left(), rect.center().y() - side * 0.16, rect.width(), side * 0.22);
        p.drawText(numRect, Qt::AlignCenter, QString::number(m_rpm));

        QFont f2 = font();
        f2.setPointSize(qMax(8, side / 20));
        p.setFont(f2);
        p.setPen(m_textMuted);
        QRectF unitRect(rect.left(), numRect.bottom() - 2, rect.width(), side * 0.11);
        p.drawText(unitRect, Qt::AlignCenter, "RPM");

        QFont f3 = font();
        f3.setPointSize(qMax(9, side / 18));
        f3.setBold(true);
        p.setFont(f3);
        p.setPen(m_accent);
        QRectF capRect(rect.left(), unitRect.bottom() + 6, rect.width(), side * 0.11);
        p.drawText(capRect, Qt::AlignCenter, m_caption);
    }

private:
    void syncSpinTimer() {
        bool shouldRun = isVisible() && m_angularVelocity > 0.01;
        if (shouldRun && !m_spinTimer->isActive()) m_spinTimer->start();
        else if (!shouldRun && m_spinTimer->isActive()) m_spinTimer->stop();
    }

    static void drawBlades(QPainter &p, const QRectF &area, double angleDeg, const QColor &color) {
        p.save();
        p.translate(area.center());
        double r = qMin(area.width(), area.height()) / 2.0;
        const int blades = 5;

        QColor bladeColor = color;
        bladeColor.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(bladeColor);
        for (int i = 0; i < blades; ++i) {
            p.save();
            p.rotate(angleDeg + i * (360.0 / blades));
            QPainterPath blade;
            blade.moveTo(0, 0);
            blade.cubicTo(r * 0.15, -r * 0.28, r * 0.85, -r * 0.32, r * 0.95, 0);
            blade.cubicTo(r * 0.85, r * 0.18, r * 0.15, r * 0.10, 0, 0);
            p.drawPath(blade);
            p.restore();
        }

        bladeColor.setAlpha(100);
        p.setBrush(bladeColor);
        p.drawEllipse(QPointF(0, 0), r * 0.12, r * 0.12);
        p.restore();
    }

    QVariantAnimation *m_anim;
    QTimer *m_spinTimer;
    double m_displayPercent = 0.0;
    double m_angle = 0.0, m_angularVelocity = 0.0;
    int m_rpm = 0;
    int m_seenMaxRpm = 1;
    QString m_caption;
    QColor m_accent{"#4d9dff"}, m_accent2{"#8b5cf6"}, m_track{"#242836"};
    QColor m_text{"#f2f4f8"}, m_textMuted{"#868da3"};
};

// --- แถบอุณหภูมิแนวนอน ไล่สีเขียว -> เหลือง -> แดงตามความร้อน พร้อมแอนิเมชันเติมสีลื่นๆ ---
class HeatBar : public QWidget {
public:
    explicit HeatBar(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(30);
        setMaximumHeight(30);
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(500);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_displayTemp = v.toDouble();
            update();
        });
    }

    void setColors(const QColor &track, const QColor &good, const QColor &warn,
                    const QColor &danger, const QColor &text) {
        m_track = track; m_good = good; m_warn = warn; m_danger = danger; m_text = text;
        update();
    }

    void setValue(double celsius, bool valid) {
        m_valid = valid;
        if (!valid) { update(); return; }
        m_anim->stop();
        m_anim->setStartValue(m_hasValue ? m_displayTemp : celsius);
        m_anim->setEndValue(celsius);
        m_anim->start();
        m_hasValue = true;
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int textW = 60;
        QRectF track(2, height() / 2.0 - 5, width() - 4 - textW, 10);
        p.setPen(Qt::NoPen);
        p.setBrush(m_track);
        p.drawRoundedRect(track, 5, 5);

        QString txt = "N/A";
        if (m_valid) {
            double frac = qBound(0.0, (m_displayTemp - m_lo) / (m_hi - m_lo), 1.0);
            QRectF fill(track.left(), track.top(), track.width() * frac, track.height());
            if (fill.width() > 1) {
                p.setBrush(colorFor(frac));
                p.drawRoundedRect(fill, 5, 5);
            }
            txt = QString::number(m_displayTemp, 'f', 1) + "°C";
        }

        QFont f = font();
        f.setBold(true);
        f.setPointSize(10);
        p.setFont(f);
        p.setPen(m_text);
        p.drawText(QRectF(width() - textW, 0, textW, height()), Qt::AlignRight | Qt::AlignVCenter, txt);
    }

private:
    QColor colorFor(double frac) const {
        if (frac < 0.5) return lerp(m_good, m_warn, frac / 0.5);
        return lerp(m_warn, m_danger, (frac - 0.5) / 0.5);
    }
    static QColor lerp(const QColor &a, const QColor &b, double t) {
        t = qBound(0.0, t, 1.0);
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                 a.greenF() + (b.greenF() - a.greenF()) * t,
                                 a.blueF() + (b.blueF() - a.blueF()) * t);
    }

    QVariantAnimation *m_anim;
    double m_displayTemp = 0.0, m_lo = 30.0, m_hi = 90.0;
    bool m_valid = false, m_hasValue = false;
    QColor m_track{"#242836"}, m_good{"#3ddc84"}, m_warn{"#f5a623"}, m_danger{"#ff5566"}, m_text{"#f2f4f8"};
};

// --- กราฟเส้นแนวโน้มรอบพัดลมย้อนหลัง (sparkline) ---
class TrendGraph : public QWidget {
public:
    explicit TrendGraph(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(42);
        setMaximumHeight(42);
    }
    void setColor(const QColor &c) { m_color = c; update(); }
    void push(double v) {
        m_data.append(v);
        while (m_data.size() > m_maxPoints) m_data.removeFirst();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_data.size() < 2) return;

        double lo = *std::min_element(m_data.begin(), m_data.end());
        double hi = *std::max_element(m_data.begin(), m_data.end());
        if (hi - lo < 1.0) hi = lo + 1.0;

        double stepX = width() / double(m_maxPoints - 1);
        double xOffset = (m_maxPoints - m_data.size()) * stepX;

        QPainterPath path, fillPath;
        for (int i = 0; i < m_data.size(); ++i) {
            double x = xOffset + i * stepX;
            double frac = (m_data[i] - lo) / (hi - lo);
            double y = height() - 4 - frac * (height() - 8);
            if (i == 0) {
                path.moveTo(x, y);
                fillPath.moveTo(x, height());
                fillPath.lineTo(x, y);
            } else {
                path.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
        }
        fillPath.lineTo(xOffset + (m_data.size() - 1) * stepX, height());
        fillPath.closeSubpath();

        QLinearGradient grad(0, 0, 0, height());
        QColor top = m_color; top.setAlpha(80);
        QColor bottom = m_color; bottom.setAlpha(0);
        grad.setColorAt(0, top);
        grad.setColorAt(1, bottom);
        p.fillPath(fillPath, grad);

        p.setPen(QPen(m_color, 2));
        p.drawPath(path);
    }

private:
    QVector<double> m_data;
    int m_maxPoints = 30;
    QColor m_color{"#4d9dff"};
};

struct FanCardWidgets {
    QFrame *card = nullptr;
    QLabel *modeLabel = nullptr;
    FanGauge *gauge = nullptr;
    HeatBar *heat = nullptr;
    TrendGraph *trend = nullptr;
    QSlider *slider = nullptr;
    QPushButton *applyBtn = nullptr;
};

struct HeaderWidgets {
    QWidget *bar = nullptr;
    QLabel *warnLabel = nullptr;
    QPushButton *fixProfileBtn = nullptr;
    QLabel *statusDot = nullptr;
    QLabel *statusText = nullptr;
    QLabel *profileLabel = nullptr;
    QLabel *readonlyBadge = nullptr;
};

FanCardWidgets buildFanCard(const QString &title) {
    FanCardWidgets w;
    w.card = new QFrame();
    w.card->setObjectName("Card");

    auto *shadow = new QGraphicsDropShadowEffect();
    shadow->setBlurRadius(36);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(0, 0, 0, 110));
    w.card->setGraphicsEffect(shadow);

    QVBoxLayout *layout = new QVBoxLayout(w.card);
    layout->setContentsMargins(22, 20, 22, 22);
    layout->setSpacing(12);

    QHBoxLayout *header = new QHBoxLayout();
    QLabel *titleLbl = new QLabel(title);
    titleLbl->setObjectName("CardTitle");
    QFont tf = titleLbl->font();
    tf.setPointSize(13);
    tf.setBold(true);
    titleLbl->setFont(tf);

    w.modeLabel = new QLabel("AUTO");
    w.modeLabel->setObjectName("ModePill");
    w.modeLabel->setAlignment(Qt::AlignCenter);

    header->addWidget(titleLbl);
    header->addStretch();
    header->addWidget(w.modeLabel);
    layout->addLayout(header);

    w.gauge = new FanGauge();
    layout->addWidget(w.gauge, 1);

    w.heat = new HeatBar();
    layout->addWidget(w.heat);

    w.trend = new TrendGraph();
    layout->addWidget(w.trend);

    w.slider = new QSlider(Qt::Horizontal);
    w.slider->setRange(0, 100);
    w.slider->setCursor(Qt::PointingHandCursor);
    layout->addWidget(w.slider);

    w.applyBtn = new QPushButton("Apply");
    w.applyBtn->setObjectName("ApplyBtn");
    w.applyBtn->setCursor(Qt::PointingHandCursor);
    w.applyBtn->setMinimumHeight(38);
    layout->addWidget(w.applyBtn);

    return w;
}

HeaderWidgets buildHeader() {
    HeaderWidgets h;
    h.bar = new QWidget();
    QHBoxLayout *l = new QHBoxLayout(h.bar);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(8);

    QLabel *title = new QLabel("NBFC UI");
    title->setObjectName("AppTitle");
    QFont tf = title->font();
    tf.setPointSize(17);
    tf.setBold(true);
    title->setFont(tf);

    h.statusDot = new QLabel();
    h.statusDot->setFixedSize(10, 10);
    auto *dotEffect = new QGraphicsOpacityEffect(h.statusDot);
    h.statusDot->setGraphicsEffect(dotEffect);
    auto *pulse = new QVariantAnimation(h.statusDot);
    pulse->setDuration(1400);
    pulse->setStartValue(0.35);
    pulse->setKeyValueAt(0.5, 1.0);
    pulse->setEndValue(0.35);
    pulse->setLoopCount(-1);
    QObject::connect(pulse, &QVariantAnimation::valueChanged, h.statusDot, [dotEffect](const QVariant &v) {
        dotEffect->setOpacity(v.toDouble());
    });
    pulse->start();

    h.statusText = new QLabel("Connecting…");
    h.statusText->setObjectName("StatusText");

    h.readonlyBadge = new QLabel("READ-ONLY");
    h.readonlyBadge->setObjectName("ReadonlyBadge");
    h.readonlyBadge->setAlignment(Qt::AlignCenter);
    h.readonlyBadge->hide();

    h.profileLabel = new QLabel("");
    h.profileLabel->setObjectName("ProfileLabel");
    QFont mf = h.profileLabel->font();
    mf.setPointSize(9);
    h.profileLabel->setFont(mf);

    l->addWidget(title);
    l->addSpacing(14);
    l->addWidget(h.statusDot);
    l->addWidget(h.statusText);
    l->addSpacing(10);
    l->addWidget(h.readonlyBadge);
    l->addStretch();
    l->addWidget(h.profileLabel);
    return h;
}

void updateModePill(QLabel *label, bool isAuto, bool isCritical = false) {
    label->setProperty("isAuto", isAuto);
    label->setProperty("isCritical", isCritical);
    QColor c = isCritical ? QColor(g_activeTheme.value("danger").toString("#ff5566"))
             : isAuto     ? QColor(g_activeTheme.value("good").toString("#3ddc84"))
                          : QColor(g_activeTheme.value("warn").toString("#f5a623"));
    label->setText(isCritical ? "⚠ CRITICAL" : (isAuto ? "AUTO" : "MANUAL"));
    label->setStyleSheet(QString(
        "background:%1; color:#10131a; border-radius:9px; padding:3px 10px; "
        "font-weight:700; font-size:10px;").arg(c.name()));
}

struct UiRefs {
    QMainWindow *window = nullptr;
    HeaderWidgets header;
    FanCardWidgets cpu;
    FanCardWidgets gpu;
};

// --- ฟังก์ชันเปลี่ยน Theme: อัปเดตทั้ง QSS ของ widget มาตรฐาน และสี custom-painted ของเกจ/แถบ/กราฟ ---
void applyTheme(UiRefs ui, const QString &themeName) {
    if (!g_config.themes.contains(themeName)) return;
    QJsonObject theme = g_config.themes[themeName].toObject();
    g_activeTheme = theme;

    QColor accent(theme.value("accent").toString("#4d9dff"));
    QColor accent2(theme.value("accent2").toString("#8b5cf6"));
    QColor cardBg(theme.value("cardBg").toString("#161922"));
    QColor cardBorder(theme.value("cardBorder").toString("#242836"));
    QColor track(theme.value("track").toString("#242836"));
    QColor textPrimary(theme.value("textPrimary").toString("#f2f4f8"));
    QColor textMuted(theme.value("textMuted").toString("#868da3"));
    QColor good(theme.value("good").toString("#3ddc84"));
    QColor warn(theme.value("warn").toString("#f5a623"));
    QColor danger(theme.value("danger").toString("#ff5566"));

    QString style = QString(
        "QMainWindow { %1 } "
        "QMenuBar { %2 } QMenuBar::item:selected { background: rgba(255,255,255,30); border-radius: 4px; } "
        "QMenu { %3 } QMenu::item:selected { background: %8; color: white; border-radius: 4px; } "
        "QWidget#CentralArea QLabel { color: %4; } "
        "QLabel#AppTitle { color: %4; } "
        "QLabel#ProfileLabel, QLabel#StatusText { color: %7; } "
        "QFrame#Card { background: %5; border: 1px solid %6; border-radius: 18px; } "
        "QLabel#CardTitle { color: %4; } "
        "QPushButton#ApplyBtn, QPushButton#AutoBtn { background: %8; color: white; border: none; "
        "border-radius: 10px; font-weight: 600; padding: 8px; } "
        "QPushButton#ApplyBtn:hover, QPushButton#AutoBtn:hover { background: %9; } "
        "QPushButton#ApplyBtn:pressed, QPushButton#AutoBtn:pressed { padding-top: 10px; padding-bottom: 6px; } "
        "%10"
    ).arg(theme.value("window").toString(), theme.value("menubar").toString(), theme.value("menu").toString(),
          textPrimary.name(), cardBg.name(), cardBorder.name(), textMuted.name(),
          accent.name(), accent2.name(), theme.value("sliders").toString());

    ui.window->setStyleSheet(style);

    for (FanCardWidgets *card : {&ui.cpu, &ui.gpu}) {
        card->gauge->setColors(accent, accent2, track, textPrimary, textMuted);
        card->heat->setColors(track, good, warn, danger, textPrimary);
        card->trend->setColor(accent);
        updateModePill(card->modeLabel, card->modeLabel->property("isAuto").toBool(),
                       card->modeLabel->property("isCritical").toBool());
    }

    ui.header.readonlyBadge->setStyleSheet(QString(
        "background:%1; color:#10131a; border-radius:9px; padding:3px 10px; "
        "font-weight:700; font-size:10px;").arg(danger.name()));
}

// --- Settings Dialog ---
class SettingsDialog : public QDialog {
public:
    SettingsDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("NBFC UI Settings");
        setMinimumWidth(480);
        applyDialogTheme();

        QVBoxLayout *mainLayout = new QVBoxLayout(this);

        // --- สถานะปัจจุบัน ---
        QGroupBox *statusBox = new QGroupBox("Current Status");
        QVBoxLayout *statusLayout = new QVBoxLayout(statusBox);
        lblStatus = new QLabel("Loading...");
        lblStatus->setWordWrap(true);
        statusLayout->addWidget(lblStatus);
        mainLayout->addWidget(statusBox);

        QFormLayout *formLayout = new QFormLayout();

        editNbfcPath = new QLineEdit(g_config.nbfcPath);
        spinInterval = new QSpinBox();
        spinInterval->setRange(500, 10000);
        spinInterval->setValue(g_config.updateIntervalMs);
        spinInterval->setSuffix(" ms");

        editCpuPath = new QLineEdit(g_config.cpuSensorPath);
        editGpuPath = new QLineEdit(g_config.gpuSensorPath);
        comboCpuZone = new QComboBox();
        comboGpuZone = new QComboBox();
        populateZoneCombo(comboCpuZone);
        populateZoneCombo(comboGpuZone);

        formLayout->addRow("NBFC Binary Path:", editNbfcPath);
        formLayout->addRow("Update Interval:", spinInterval);
        formLayout->addRow("CPU Sensor Path:", editCpuPath);
        formLayout->addRow("  ↳ Detected sensors:", comboCpuZone);
        formLayout->addRow("GPU Sensor Path (fallback):", editGpuPath);
        formLayout->addRow("  ↳ Detected sensors:", comboGpuZone);

        chkAutoGpu = new QCheckBox("ตรวจจับอุณหภูมิ GPU อัตโนมัติด้วย nvidia-smi ถ้ามีการ์ดจอแยก NVIDIA\n(ปิดถ้าต้องการใช้ GPU Sensor Path ด้านบนเสมอ)");
        chkAutoGpu->setChecked(g_config.autoDetectSensors);
        formLayout->addRow(chkAutoGpu);

        connect(comboCpuZone, QOverload<int>::of(&QComboBox::activated), [this](int idx) {
            if (idx >= 0) editCpuPath->setText(comboCpuZone->itemData(idx).toString());
        });
        connect(comboGpuZone, QOverload<int>::of(&QComboBox::activated), [this](int idx) {
            if (idx >= 0) editGpuPath->setText(comboGpuZone->itemData(idx).toString());
        });

        // NBFC Profile Section
        QGroupBox *nbfcBox = new QGroupBox("NBFC Management");
        QVBoxLayout *nbfcLayout = new QVBoxLayout(nbfcBox);

        QHBoxLayout *profileLayout = new QHBoxLayout();
        comboProfiles = new QComboBox();
        refreshProfiles();
        QPushButton *btnApplyProfile = new QPushButton("Apply Profile");
        profileLayout->addWidget(new QLabel("Profile:"));
        profileLayout->addWidget(comboProfiles, 1);
        profileLayout->addWidget(btnApplyProfile);

        QHBoxLayout *serviceLayout = new QHBoxLayout();
        QPushButton *btnStart = new QPushButton("Start Service");
        QPushButton *btnStop = new QPushButton("Stop Service");
        QPushButton *btnRestart = new QPushButton("Restart");
        QPushButton *btnRefresh = new QPushButton("Refresh Status");
        serviceLayout->addWidget(btnStart);
        serviceLayout->addWidget(btnStop);
        serviceLayout->addWidget(btnRestart);
        serviceLayout->addWidget(btnRefresh);

        nbfcLayout->addLayout(profileLayout);
        nbfcLayout->addLayout(serviceLayout);

        mainLayout->addLayout(formLayout);
        mainLayout->addWidget(nbfcBox);

        QHBoxLayout *buttons = new QHBoxLayout();
        QPushButton *btnSave = new QPushButton("Save & Close");
        QPushButton *btnCancel = new QPushButton("Cancel");
        buttons->addStretch();
        buttons->addWidget(btnSave);
        buttons->addWidget(btnCancel);
        mainLayout->addLayout(buttons);

        connect(btnSave, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(btnApplyProfile, &QPushButton::clicked, this, &SettingsDialog::applyProfile);
        connect(btnStart, &QPushButton::clicked, [this]() { QProcess::execute(g_config.nbfcPath, {"start"}); refreshStatus(); });
        connect(btnStop, &QPushButton::clicked, [this]() { QProcess::execute(g_config.nbfcPath, {"stop"}); refreshStatus(); });
        connect(btnRestart, &QPushButton::clicked, [this]() { QProcess::execute(g_config.nbfcPath, {"restart"}); refreshStatus(); });
        connect(btnRefresh, &QPushButton::clicked, this, &SettingsDialog::refreshStatus);

        refreshStatus();
    }

private:
    QLineEdit *editNbfcPath, *editCpuPath, *editGpuPath;
    QSpinBox *spinInterval;
    QComboBox *comboProfiles, *comboCpuZone, *comboGpuZone;
    QCheckBox *chkAutoGpu;
    QLabel *lblStatus;

    // ให้หน้าต่าง Settings ใช้โทนสีเดียวกับธีมที่เลือกไว้ในหน้าหลัก แทนสี Qt ดีฟอลต์
    void applyDialogTheme() {
        QColor cardBg(g_activeTheme.value("cardBg").toString("#161922"));
        QColor cardBorder(g_activeTheme.value("cardBorder").toString("#242836"));
        QColor track(g_activeTheme.value("track").toString("#242836"));
        QColor textPrimary(g_activeTheme.value("textPrimary").toString("#f2f4f8"));
        QColor textMuted(g_activeTheme.value("textMuted").toString("#868da3"));
        QColor accent(g_activeTheme.value("accent").toString("#4d9dff"));
        QColor accent2(g_activeTheme.value("accent2").toString("#8b5cf6"));

        setStyleSheet(QString(
            "QDialog { background: %1; } "
            "QLabel { color: %2; background: transparent; } "
            "QGroupBox { color: %2; border: 1px solid %3; border-radius: 10px; "
            "margin-top: 14px; padding-top: 12px; font-weight: 600; } "
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; } "
            "QLineEdit, QComboBox, QSpinBox { background: %4; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 4px 8px; } "
            "QComboBox QAbstractItemView { background: %4; color: %2; selection-background-color: %6; } "
            "QCheckBox { color: %5; } "
            "QPushButton { background: %6; color: white; border: none; border-radius: 8px; "
            "padding: 7px 16px; font-weight: 600; } "
            "QPushButton:hover { background: %7; } "
            "QPushButton:disabled { background: %3; color: %5; }"
        ).arg(cardBg.name(), textPrimary.name(), cardBorder.name(), track.name(),
              textMuted.name(), accent.name(), accent2.name()));
    }

    void populateZoneCombo(QComboBox *combo) {
        for (const auto &zone : listThermalZones())
            combo->addItem(zone.label, zone.path);
    }

    void refreshProfiles() {
        QProcess proc;
        proc.start(g_config.nbfcPath, {"config", "-l"});
        if (proc.waitForFinished()) {
            QString out = proc.readAllStandardOutput();
            QStringList list = out.split("\n", Qt::SkipEmptyParts);
            comboProfiles->addItems(list);

            QProcess statusProc;
            statusProc.start(g_config.nbfcPath, {"status"});
            if (statusProc.waitForFinished()) {
                QString statusOut = statusProc.readAllStandardOutput();
                QRegularExpression re("Selected Config Name\\s+:\\s+(.*)");
                auto match = re.match(statusOut);
                if (match.hasMatch()) {
                    comboProfiles->setCurrentText(match.captured(1).trimmed());
                }
            }
        }
    }

    void refreshStatus() {
        QProcess proc;
        proc.start(g_config.nbfcPath, {"status", "-a"});
        if (!proc.waitForFinished(1000) || proc.exitCode() != 0) {
            lblStatus->setText("⚠ ติดต่อ nbfc_service ไม่ได้ — ตรวจสอบว่า service ทำงานอยู่หรือไม่ (ลองกด Start Service)");
            return;
        }
        QString out = proc.readAllStandardOutput();
        QRegularExpression reReadonly("Read-only\\s+:\\s+(true|false)");
        QRegularExpression reProfile("Selected Config Name\\s+:\\s+(.*)");
        QRegularExpression reFanCount("Fan Display Name");

        QString readonly = reReadonly.match(out).captured(1);
        QString profile = reProfile.match(out).captured(1).trimmed();
        int fanCount = out.count(reFanCount);

        bool nvidiaPresent = QProcess::execute("nvidia-smi", {"-L"}) == 0;

        lblStatus->setText(QString(
            "✓ Service: กำลังทำงาน\n"
            "Profile: %1\n"
            "Read-only mode: %2\n"
            "จำนวนพัดลมที่ตรวจพบ: %3\n"
            "GPU แยก (NVIDIA): %4")
            .arg(profile.isEmpty() ? "N/A" : profile)
            .arg(readonly == "true" ? "เปิด (ปรับความเร็วพัดลมไม่ได้)" : "ปิด")
            .arg(fanCount)
            .arg(nvidiaPresent ? "พบ" : "ไม่พบ"));
    }

    void applyProfile() {
        QString profile = comboProfiles->currentText();
        if (profile.isEmpty()) return;
        QProcess::execute(g_config.nbfcPath, {"config", "-s", profile});
        QMessageBox::information(this, "NBFC", "Profile applied: " + profile);
        refreshStatus();
    }

    void saveSettings() {
        g_config.nbfcPath = editNbfcPath->text();
        g_config.updateIntervalMs = spinInterval->value();
        g_config.cpuSensorPath = editCpuPath->text();
        g_config.gpuSensorPath = editGpuPath->text();
        g_config.autoDetectSensors = chkAutoGpu->isChecked();
        g_config.save();
        accept();
    }
};

// --- ควบคุมพัดลม ---
void setFanSpeed(int index, int percent) {
    QProcess::execute(g_config.nbfcPath, {"set", "-f", QString::number(index), "-s", QString::number(percent)});
}

void setAutoMode() {
    QProcess::execute(g_config.nbfcPath, {"set", "-f", "0", "-a"});
    QProcess::execute(g_config.nbfcPath, {"set", "-f", "1", "-a"});
}

// --- อ่านอุณหภูมิโดยตรงจากระบบ (sysfs thermal zone) ---
bool getTempValue(const QString &path, double &out) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        double temp = QString(file.readAll()).trimmed().toDouble() / 1000.0;
        file.close();
        if (temp > 0 && temp < 150) { out = temp; return true; }
    }
    return false;
}

// --- อุณหภูมิ GPU: ถ้ามีการ์ดจอแยก NVIDIA ให้อ่านจาก nvidia-smi (แม่นยำกว่า thermal_zone
// ที่มักเป็นโซนของ Intel platform framework ซึ่งไม่ใช่อุณหภูมิ GPU จริง) ไม่งั้น fallback ไป sysfs ---
bool getGpuTempValue(double &out) {
    // nvidia-smi เป็นโปรเซสหนัก: อ่านทุก ~6 วินาที แล้วใช้ค่าที่แคชไว้ในรอบอื่น
    static QElapsedTimer lastRead;
    static double cached = 0;
    static bool cachedOk = false;
    if (g_config.autoDetectSensors && lastRead.isValid() && lastRead.elapsed() < 6000 && cachedOk) {
        out = cached;
        return true;
    }
    if (g_config.autoDetectSensors) {
        lastRead.start();
        QProcess proc;
        proc.start("nvidia-smi", {"--query-gpu=temperature.gpu", "--format=csv,noheader,nounits"});
        if (proc.waitForFinished(500) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
            bool ok = false;
            double t = QString(proc.readAllStandardOutput()).trimmed().toDouble(&ok);
            if (ok && t > 0 && t < 150) { out = t; cached = t; cachedOk = true; return true; }
        }
        cachedOk = false;
    }
    return getTempValue(g_config.gpuSensorPath, out);
}

// --- ตรวจความถูกต้องของระบบ: โปรไฟล์ตรงรุ่น, พัดลมหมุนจริง, ตัวควบคุมซ้อนกัน ---
QString readDmiProduct() {
    QFile f("/sys/class/dmi/id/product_name");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString(f.readAll()).trimmed();
}

QString normalizeModel(QString s) {
    return s.toLower().remove(QRegularExpression("[^a-z0-9]"));
}

// หาโปรไฟล์ NBFC ที่ชื่อรุ่นตรงกับ DMI (เช่น "Nitro AN515-58" -> "Acer Nitro AN515-58"); ว่างถ้าไม่เจอ
QString findMatchingProfile(const QString &dmi) {
    static QStringList cache;
    if (cache.isEmpty()) {
        QProcess p;
        p.start(g_config.nbfcPath, {"config", "-l"});
        if (p.waitForFinished(3000)) cache = QString(p.readAllStandardOutput()).split("\n", Qt::SkipEmptyParts);
        for (QString &s : cache) s = s.trimmed();
    }
    QString key = normalizeModel(dmi);
    if (key.isEmpty()) return {};
    QString best;
    for (const QString &p : cache) {
        if (normalizeModel(p) == key) return p;
        if (normalizeModel(p).endsWith(key) && (best.isEmpty() || p.size() < best.size())) best = p;
    }
    return best;
}

// เปลี่ยนโปรไฟล์ (ต้องใช้สิทธิ์ root) แล้วรีสตาร์ทเซอร์วิส
bool switchProfile(const QString &profile) {
    QString cmd = QString("%1 config -s '%2' && %1 restart").arg(g_config.nbfcPath, profile);
    int rc = geteuid() == 0 ? QProcess::execute("sh", {"-c", cmd})
                            : QProcess::execute("pkexec", {"sh", "-c", cmd});
    return rc == 0;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    g_config.load();

    QUiLoader loader;
    QFile file(":/UI.ui");
    if (!file.open(QFile::ReadOnly)) {
        QMessageBox::critical(nullptr, "Error", "Could not load UI from resources!");
        return -1;
    }
    QMainWindow *mainWindow = qobject_cast<QMainWindow*>(loader.load(&file));
    file.close();

    if (!mainWindow) return -1;

    mainWindow->setWindowIcon(QIcon(":/icon.png"));

    // --- สร้างหน้าจอหลักใหม่ทั้งหมดด้วยโค้ด (แทนเลย์เอาต์แบบตำแหน่งตายตัวเดิม) ---
    QWidget *central = new QWidget();
    central->setObjectName("CentralArea");
    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(26, 18, 26, 22);
    rootLayout->setSpacing(18);

    HeaderWidgets header = buildHeader();
    rootLayout->addWidget(header.bar);

    // แถบเตือน: โปรไฟล์ไม่ตรงรุ่น / พัดลมไม่หมุน / ตัวควบคุมซ้อน / ร้อนเกิน
    QFrame *warnBar = new QFrame();
    QHBoxLayout *warnLayout = new QHBoxLayout(warnBar);
    warnLayout->setContentsMargins(12, 8, 12, 8);
    header.warnLabel = new QLabel();
    header.warnLabel->setWordWrap(true);
    header.fixProfileBtn = new QPushButton("สลับเป็นโปรไฟล์ที่ตรงรุ่น");
    header.fixProfileBtn->setCursor(Qt::PointingHandCursor);
    header.fixProfileBtn->hide();
    warnLayout->addWidget(header.warnLabel, 1);
    warnLayout->addWidget(header.fixProfileBtn);
    warnBar->setStyleSheet("QFrame { background:#3a2a10; border:1px solid #f5a623; border-radius:8px; } "
                           "QLabel { color:#ffd98a; border:none; background:transparent; } "
                           "QPushButton { background:#f5a623; color:#10131a; border-radius:6px; padding:6px 12px; font-weight:700; }");
    warnBar->hide();
    rootLayout->addWidget(warnBar);

    QHBoxLayout *cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(22);
    FanCardWidgets cpuCard = buildFanCard("CPU");
    FanCardWidgets gpuCard = buildFanCard("GPU");
    cardsLayout->addWidget(cpuCard.card);
    cardsLayout->addWidget(gpuCard.card);
    rootLayout->addLayout(cardsLayout, 1);

    QPushButton *btnAuto = new QPushButton("⚡  Auto Mode (Both Fans)");
    btnAuto->setObjectName("AutoBtn");
    btnAuto->setCursor(Qt::PointingHandCursor);
    btnAuto->setMinimumHeight(46);
    rootLayout->addWidget(btnAuto);

    mainWindow->setCentralWidget(central);

    UiRefs ui{mainWindow, header, cpuCard, gpuCard};
    applyTheme(ui, g_config.currentTheme);

    QObject::connect(cpuCard.applyBtn, &QPushButton::clicked, [slider = cpuCard.slider]() { setFanSpeed(0, slider->value()); });
    QObject::connect(gpuCard.applyBtn, &QPushButton::clicked, [slider = gpuCard.slider]() { setFanSpeed(1, slider->value()); });
    QObject::connect(btnAuto, &QPushButton::clicked, []() { setAutoMode(); });
    QObject::connect(header.fixProfileBtn, &QPushButton::clicked, [mainWindow, fixBtn = header.fixProfileBtn]() {
        QString target = fixBtn->text().mid(QString("สลับเป็น ").size());
        if (QMessageBox::question(mainWindow, "เปลี่ยนโปรไฟล์ NBFC",
                "จะเปลี่ยนโปรไฟล์เป็น \"" + target + "\" และรีสตาร์ท nbfc_service (ต้องใช้สิทธิ์ผู้ดูแลระบบ) ใช่ไหม?")
            == QMessageBox::Yes) {
            if (!switchProfile(target))
                QMessageBox::warning(mainWindow, "NBFC", "เปลี่ยนโปรไฟล์ไม่สำเร็จ");
        }
    });

    // เชื่อมต่อ Menu Actions
    QStringList themes = {"Dark", "Light", "Blue", "Custom"};
    for (const QString &t : themes) {
        auto act = mainWindow->findChild<QAction*>("action" + t);
        if (act) QObject::connect(act, &QAction::triggered, [ui, t]() {
            g_config.currentTheme = t.toLower();
            applyTheme(ui, g_config.currentTheme);
            g_config.save();
        });
    }

    auto actSettings = mainWindow->findChild<QAction*>("actionSettings");
    if (actSettings) QObject::connect(actSettings, &QAction::triggered, [mainWindow]() {
        SettingsDialog dlg(mainWindow);
        dlg.exec();
    });

    auto actAbout = mainWindow->findChild<QAction*>("actionAbout");
    if (actAbout) QObject::connect(actAbout, &QAction::triggered, [mainWindow]() {
        QMessageBox::about(mainWindow, "About", "NBFC UI v" + APP_VERSION + "\nFull Control & Monitoring for Linux\nDeveloped with Qt5");
    });

    // --- Timer อัปเดตข้อมูล ---
    QTimer *timer = new QTimer(mainWindow);
    auto updateStatus = [=]() {
        double cpuT = 0, gpuT = 0;
        bool cpuOk = getTempValue(g_config.cpuSensorPath, cpuT);
        bool gpuOk = getGpuTempValue(gpuT);
        cpuCard.heat->setValue(cpuT, cpuOk);
        gpuCard.heat->setValue(gpuT, gpuOk);

        QProcess proc;
        proc.start(g_config.nbfcPath, {"status", "-a"});
        bool serviceOk = proc.waitForFinished(1000) && proc.exitCode() == 0;

        if (serviceOk) {
            QString out = proc.readAllStandardOutput();

            // "Current Fan Speed" คือรอบพัดลม (RPM) ไม่ใช่เปอร์เซ็นต์ ส่วน "Target Fan Speed" คือ %
            QRegularExpression reRpm("Current Fan Speed\\s+:\\s+(\\d+\\.\\d+)");
            QRegularExpression reSteps("Fan Speed Steps\\s+:\\s+(\\d+)");
            QRegularExpression reTarget("Target Fan Speed\\s+:\\s+(\\d+\\.\\d+)");
            QRegularExpression reAuto("Auto Control Enabled\\s+:\\s+(true|false)");
            QRegularExpression reCritical("Critical Mode Enabled\\s+:\\s+(true|false)");
            QRegularExpression reReadonly("Read-only\\s+:\\s+(true|false)");

            QList<double> rpms, targets;
            QList<bool> autos, criticals;
            for (auto it = reRpm.globalMatch(out); it.hasNext();) rpms << it.next().captured(1).toDouble();
            QList<int> steps;
            for (auto it = reSteps.globalMatch(out); it.hasNext();) steps << it.next().captured(1).toInt();
            // โปรไฟล์ที่ MaxSpeedValueRead > 100 (เช่น AN515-58 = 7317) รายงาน Current เป็น % ของรอบสูงสุด
            // ต้องแปลงเป็น RPM จริง; โปรไฟล์ที่ steps = 100 รายงานเป็นค่า RPM ดิบอยู่แล้ว
            for (int i = 0; i < rpms.size() && i < steps.size(); ++i)
                if (steps[i] > 100) rpms[i] = rpms[i] * steps[i] / 100.0;
            for (auto it = reTarget.globalMatch(out); it.hasNext();) targets << it.next().captured(1).toDouble();
            for (auto it = reAuto.globalMatch(out); it.hasNext();) autos << (it.next().captured(1) == "true");
            for (auto it = reCritical.globalMatch(out); it.hasNext();) criticals << (it.next().captured(1) == "true");

            bool readOnly = reReadonly.match(out).captured(1) == "true";
            header.readonlyBadge->setVisible(readOnly);
            for (QWidget *w : {static_cast<QWidget*>(cpuCard.slider), static_cast<QWidget*>(gpuCard.slider),
                                static_cast<QWidget*>(cpuCard.applyBtn), static_cast<QWidget*>(gpuCard.applyBtn),
                                static_cast<QWidget*>(btnAuto)})
                w->setEnabled(!readOnly);

            QList<const FanCardWidgets*> cards = {&cpuCard, &gpuCard};
            for (int i = 0; i < cards.size(); ++i) {
                int rpm = i < rpms.size() ? qRound(rpms[i]) : 0;
                int target = i < targets.size() ? qRound(targets[i]) : 0;
                bool isAuto = i < autos.size() ? autos[i] : true;
                bool isCritical = i < criticals.size() ? criticals[i] : false;

                cards[i]->gauge->setValues(target, rpm, QString("Target %1%").arg(target));
                cards[i]->trend->push(rpm);
                updateModePill(cards[i]->modeLabel, isAuto, isCritical);
                if (cards[i]->slider && !cards[i]->slider->isSliderDown())
                    cards[i]->slider->setValue(target);
            }

            QRegularExpression reProfile("Selected Config Name\\s+:\\s+(.*)");
            auto m = reProfile.match(out);
            QString activeProfile = m.hasMatch() ? m.captured(1).trimmed() : QString();
            QRegularExpression reNbfcTemp("Temperature\\s+:\\s+(\\d+\\.\\d+)");
            auto tm = reNbfcTemp.match(out);
            QString nbfcTemp = tm.hasMatch() ? QString("  ·  NBFC temp %1°C").arg(qRound(tm.captured(1).toDouble())) : QString();
            header.profileLabel->setText(m.hasMatch() ? "Profile: " + activeProfile + nbfcTemp : "");

            // --- ตรวจสอบและเตือน ---
            QStringList warns;
            static QString dmi = readDmiProduct();
            static QString matching = findMatchingProfile(dmi);
            bool mismatch = !matching.isEmpty() && !activeProfile.isEmpty() && matching != activeProfile;
            if (mismatch)
                warns << QString("โปรไฟล์ไม่ตรงรุ่น: เครื่องคือ \"%1\" แต่ NBFC ใช้ \"%2\" — รีจิสเตอร์ EC อาจไม่ตรง").arg(dmi, activeProfile);
            header.fixProfileBtn->setVisible(mismatch);
            header.fixProfileBtn->setText("สลับเป็น " + matching);

            // พัดลมถูกสั่งแต่รอบเป็น 0 ติดกันหลายรอบ = พัดลมเสีย/ไม่รองรับการอ่านรอบ
            static int stalled[2] = {0, 0};
            const char *fanNames[2] = {"CPU", "GPU"};
            for (int i = 0; i < 2; ++i) {
                bool bad = i < rpms.size() && i < targets.size() && targets[i] >= 30 && rpms[i] < 1;
                stalled[i] = bad ? stalled[i] + 1 : 0;
                if (stalled[i] >= 5)
                    warns << QString("พัดลม %1 อ่านได้ 0 RPM ทั้งที่สั่ง %2% — พัดลมอาจเสีย/ไม่หมุน หรือโปรไฟล์ไม่รองรับ")
                                 .arg(fanNames[i]).arg(qRound(targets[i]));
            }

            // Watchdog: โหมด manual แต่ร้อนเกิน 90°C และพัดลมยังไม่เต็ม -> คืนให้ auto ทันที
            static QElapsedTimer lastGuard;
            double hot = qMax(cpuOk ? cpuT : 0.0, gpuOk ? gpuT : 0.0);
            bool anyManual = false;
            for (int i = 0; i < 2; ++i)
                if (i < autos.size() && !autos[i] && i < targets.size() && targets[i] < 100) anyManual = true;
            if (hot >= 90.0 && anyManual && (!lastGuard.isValid() || lastGuard.elapsed() > 10000)) {
                lastGuard.start();
                setAutoMode();
                warns << QString("อุณหภูมิ %1°C สูงเกิน 90°C ขณะ manual — สลับกลับ Auto ให้อัตโนมัติ").arg(hot, 0, 'f', 0);
            }

            warnBar->setVisible(!warns.isEmpty());
            header.warnLabel->setText(warns.join("\n"));

            header.statusDot->setStyleSheet(QString("background:%1; border-radius:5px;")
                .arg(QColor(g_activeTheme.value("good").toString("#3ddc84")).name()));
            header.statusText->setText("Connected");
        } else {
            header.statusDot->setStyleSheet(QString("background:%1; border-radius:5px;")
                .arg(QColor(g_activeTheme.value("danger").toString("#ff5566")).name()));
            header.statusText->setText("Service unreachable");
        }
    };
    QObject::connect(timer, &QTimer::timeout, updateStatus);
    timer->start(g_config.updateIntervalMs);
    updateStatus();

    mainWindow->show();

    return app.exec();
}
