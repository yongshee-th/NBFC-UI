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
#include <QtCore/QProcess>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QDir>

const QString APP_VERSION = "2.3";

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

// --- Settings Dialog ---
class SettingsDialog : public QDialog {
public:
    SettingsDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("NBFC UI Settings");
        setMinimumWidth(450);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        QFormLayout *formLayout = new QFormLayout();

        editNbfcPath = new QLineEdit(g_config.nbfcPath);
        spinInterval = new QSpinBox();
        spinInterval->setRange(500, 10000);
        spinInterval->setValue(g_config.updateIntervalMs);
        spinInterval->setSuffix(" ms");

        editCpuPath = new QLineEdit(g_config.cpuSensorPath);
        editGpuPath = new QLineEdit(g_config.gpuSensorPath);

        formLayout->addRow("NBFC Binary Path:", editNbfcPath);
        formLayout->addRow("Update Interval:", spinInterval);
        formLayout->addRow("CPU Sensor Path:", editCpuPath);
        formLayout->addRow("GPU Sensor Path:", editGpuPath);

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
        serviceLayout->addWidget(btnStart);
        serviceLayout->addWidget(btnStop);
        serviceLayout->addWidget(btnRestart);

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
        connect(btnStart, &QPushButton::clicked, []() { QProcess::execute(g_config.nbfcPath, {"start"}); });
        connect(btnStop, &QPushButton::clicked, []() { QProcess::execute(g_config.nbfcPath, {"stop"}); });
        connect(btnRestart, &QPushButton::clicked, []() { QProcess::execute(g_config.nbfcPath, {"restart"}); });
    }

private:
    QLineEdit *editNbfcPath, *editCpuPath, *editGpuPath;
    QSpinBox *spinInterval;
    QComboBox *comboProfiles;

    void refreshProfiles() {
        QProcess proc;
        proc.start(g_config.nbfcPath, {"config", "-l"});
        if (proc.waitForFinished()) {
            QString out = proc.readAllStandardOutput();
            QStringList list = out.split("\n", Qt::SkipEmptyParts);
            comboProfiles->addItems(list);
            
            // Try to find current config
            QProcess statusProc;
            statusProc.start(g_config.nbfcPath, {"status"});
            if (statusProc.waitForFinished()) {
                QString statusOut = statusProc.readAllStandardOutput();
                QRegularExpression re("Selected config file\\s+:\\s+(.*)");
                auto match = re.match(statusOut);
                if (match.hasMatch()) {
                    comboProfiles->setCurrentText(match.captured(1).trimmed());
                }
            }
        }
    }

    void applyProfile() {
        QString profile = comboProfiles->currentText();
        if (profile.isEmpty()) return;
        QProcess::execute(g_config.nbfcPath, {"config", "-s", profile});
        QMessageBox::information(this, "NBFC", "Profile applied: " + profile);
    }

    void saveSettings() {
        g_config.nbfcPath = editNbfcPath->text();
        g_config.updateIntervalMs = spinInterval->value();
        g_config.cpuSensorPath = editCpuPath->text();
        g_config.gpuSensorPath = editGpuPath->text();
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

// --- อ่านอุณหภูมิโดยตรงจากระบบ ---
QString getTemp(const QString& path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QString rawData = file.readAll().trimmed();
        double temp = rawData.toDouble() / 1000.0;
        file.close();
        if (temp > 0 && temp < 150) {
            return QString::number(temp, 'f', 1) + "°C";
        }
    }
    return "N/A";
}

// --- ฟังก์ชันเปลี่ยน Theme ---
void applyTheme(QMainWindow* window, QString themeName) {
    if (!g_config.themes.contains(themeName)) return;
    QJsonObject theme = g_config.themes[themeName].toObject();

    QString style = QString(
        "QMainWindow { %1 } "
        "QMenuBar { %2 } "
        "QMenu { %3 } "
        "QGroupBox { %4 } "
        "QLabel { %5 } "
        "QLabel#Speed_CPU, QLabel#Speed_GPU { %6 } "
        "QPushButton { %7 } "
        "%8"
    ).arg(theme["window"].toString(), theme["menubar"].toString(), theme["menu"].toString(),
          theme["groupbox"].toString(), theme["labels"].toString(), theme["values"].toString(), 
          theme["buttons"].toString(), theme["sliders"].toString());

    window->setStyleSheet(style);
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
    applyTheme(mainWindow, g_config.currentTheme);

    // ดึง UI Objects
    auto lblSpeedCpu = mainWindow->findChild<QLabel*>("Speed_CPU");
    auto lblSpeedGpu = mainWindow->findChild<QLabel*>("Speed_GPU");
    auto lblTempCpu = mainWindow->findChild<QLabel*>("Temperature_CPU");
    auto lblTempGpu = mainWindow->findChild<QLabel*>("Temperature_GPU");
    
    QList<QLabel*> labels = {lblSpeedCpu, lblSpeedGpu, lblTempCpu, lblTempGpu};
    for(auto l : labels) { if(l) { l->setWordWrap(false); l->setAlignment(Qt::AlignCenter); } }

    auto sliderCpu = mainWindow->findChild<QSlider*>("horizontalSlider_CPU");
    auto sliderGpu = mainWindow->findChild<QSlider*>("horizontalSlider_GPU");
    auto btnCpu = mainWindow->findChild<QPushButton*>("Apply_CPU");
    auto btnGpu = mainWindow->findChild<QPushButton*>("Apply_GPU");
    auto btnAuto = mainWindow->findChild<QPushButton*>("Apply_Auto");

    if (btnCpu) QObject::connect(btnCpu, &QPushButton::clicked, [sliderCpu]() { if(sliderCpu) setFanSpeed(0, sliderCpu->value()); });
    if (btnGpu) QObject::connect(btnGpu, &QPushButton::clicked, [sliderGpu]() { if(sliderGpu) setFanSpeed(1, sliderGpu->value()); });
    if (btnAuto) QObject::connect(btnAuto, &QPushButton::clicked, []() { setAutoMode(); });

    // เชื่อมต่อ Menu Actions
    QStringList themes = {"Dark", "Light", "Blue", "Custom"};
    for(const QString &t : themes) {
        auto act = mainWindow->findChild<QAction*>("action" + t);
        if(act) QObject::connect(act, &QAction::triggered, [mainWindow, t](){ 
            g_config.currentTheme = t.toLower();
            applyTheme(mainWindow, g_config.currentTheme);
            g_config.save();
        });
    }

    auto actSettings = mainWindow->findChild<QAction*>("actionSettings");
    if(actSettings) QObject::connect(actSettings, &QAction::triggered, [mainWindow](){
        SettingsDialog dlg(mainWindow);
        dlg.exec();
    });

    auto actAbout = mainWindow->findChild<QAction*>("actionAbout");
    if(actAbout) QObject::connect(actAbout, &QAction::triggered, [mainWindow](){
        QMessageBox::about(mainWindow, "About", "NBFC UI v" + APP_VERSION + "\nFull Control & Monitoring for Linux\nDeveloped with Qt5");
    });

    // --- Timer อัปเดตข้อมูล ---
    QTimer *timer = new QTimer(mainWindow);
    QObject::connect(timer, &QTimer::timeout, [=]() {
        if(lblTempCpu) lblTempCpu->setText(getTemp(g_config.cpuSensorPath));
        if(lblTempGpu) lblTempGpu->setText(getTemp(g_config.gpuSensorPath));
        
        QProcess proc;
        proc.start(g_config.nbfcPath, {"status", "-a"});
        if (proc.waitForFinished(1000)) {
            QString out = proc.readAllStandardOutput();
            QRegularExpression re("Current Fan Speed\\s+:\\s+(\\d+\\.\\d+)");
            auto matches = re.globalMatch(out);
            
            int i = 0;
            while (matches.hasNext()) {
                auto match = matches.next();
                QString val = match.captured(1).split(".")[0].trimmed() + "%";
                if (i == 0 && lblSpeedCpu) lblSpeedCpu->setText(val);
                if (i == 1 && lblSpeedGpu) lblSpeedGpu->setText(val);
                i++;
            }
        }
    });
    timer->start(g_config.updateIntervalMs);

    mainWindow->show();
    return app.exec();
}
