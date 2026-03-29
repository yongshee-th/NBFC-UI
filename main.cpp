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
#include <QtCore/QProcess>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

// --- ควบคุมพัดลม ---
void setFanSpeed(int index, int percent) { 
    QProcess::execute("nbfc", {"set", "-f", QString::number(index), "-s", QString::number(percent)}); 
}

void setAutoMode() { 
    QProcess::execute("nbfc", {"set", "-f", "0", "-a"}); 
    QProcess::execute("nbfc", {"set", "-f", "1", "-a"}); 
}

// --- อ่านอุณหภูมิและล้างค่าขยะ ---
QString getTemp(int zone) {
    QFile file("/sys/class/thermal/thermal_zone" + QString::number(zone) + "/temp");
    if (file.open(QIODevice::ReadOnly)) {
        QString rawData = file.readAll().trimmed(); // ตัด \n ออกทันที
        double temp = rawData.toDouble() / 1000.0;
        file.close();
        return QString::number(temp, 'f', 1) + "°C";
    }
    return "N/A";
}

// --- ฟังก์ชันเปลี่ยน Theme ---
void applyThemeFromJson(QMainWindow* window, QString themeName) {
    QString configPath = QCoreApplication::applicationDirPath() + "/Config.json";
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QJsonObject theme = root["themes"].toObject()[themeName].toObject();
    file.close();

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
    QUiLoader loader;
    QFile file(QCoreApplication::applicationDirPath() + "/UI.ui");
    if (!file.open(QFile::ReadOnly)) return -1;
    QMainWindow *mainWindow = qobject_cast<QMainWindow*>(loader.load(&file));
    file.close();

    // เริ่มต้นที่ Dark
    applyThemeFromJson(mainWindow, "dark");

    // ดึง UI Objects
    auto lblSpeedCpu = mainWindow->findChild<QLabel*>("Speed_CPU");
    auto lblSpeedGpu = mainWindow->findChild<QLabel*>("Speed_GPU");
    auto lblTempCpu = mainWindow->findChild<QLabel*>("Temperature_CPU");
    auto lblTempGpu = mainWindow->findChild<QLabel*>("Temperature_GPU");
    
    // ตั้งค่าป้องกัน Newline และจัดกลาง
    QList<QLabel*> labels = {lblSpeedCpu, lblSpeedGpu, lblTempCpu, lblTempGpu};
    for(auto l : labels) { if(l) { l->setWordWrap(false); l->setAlignment(Qt::AlignCenter); } }

    auto sliderCpu = mainWindow->findChild<QSlider*>("horizontalSlider_CPU");
    auto sliderGpu = mainWindow->findChild<QSlider*>("horizontalSlider_GPU");
    auto btnCpu = mainWindow->findChild<QPushButton*>("Apply_CPU");
    auto btnGpu = mainWindow->findChild<QPushButton*>("Apply_GPU");
    auto btnAuto = mainWindow->findChild<QPushButton*>("Apply_Auto");

    if (btnCpu) QObject::connect(btnCpu, &QPushButton::clicked, [sliderCpu]() { setFanSpeed(0, sliderCpu->value()); });
    if (btnGpu) QObject::connect(btnGpu, &QPushButton::clicked, [sliderGpu]() { setFanSpeed(1, sliderGpu->value()); });
    if (btnAuto) QObject::connect(btnAuto, &QPushButton::clicked, []() { setAutoMode(); });

    // เชื่อมต่อ Menu Actions ให้ครบทุกธีม
    QStringList themes = {"Dark", "Light", "Blue", "Custom"};
    for(const QString &t : themes) {
        auto act = mainWindow->findChild<QAction*>("action" + t);
        if(act) QObject::connect(act, &QAction::triggered, [mainWindow, t](){ 
            applyThemeFromJson(mainWindow, t.toLower()); 
        });
    }

    auto actAbout = mainWindow->findChild<QAction*>("actionAbout");
    if(actAbout) QObject::connect(actAbout, &QAction::triggered, [mainWindow](){
        QMessageBox::about(mainWindow, "About", "NBFC Fan Control v2.0\nTheme System & Real-time Monitor");
    });

    // --- Timer อัปเดตข้อมูล ---
    QTimer *timer = new QTimer(mainWindow);
    QObject::connect(timer, &QTimer::timeout, [=]() {
        if(lblTempCpu) lblTempCpu->setText(getTemp(0));
        if(lblTempGpu) lblTempGpu->setText(getTemp(1));
        
        QProcess proc;
        proc.start("nbfc", {"status", "-a"});
        if (proc.waitForFinished(1500)) {
            QString out = proc.readAllStandardOutput();
            QRegularExpression re("Current Fan Speed\\s+:\\s+(\\d+\\.\\d+)");
            auto matches = re.globalMatch(out);
            
            int i = 0;
            while (matches.hasNext()) {
                auto match = matches.next();
                // ล้างช่องว่างและบรรทัดใหม่ก่อนต่อ %
                QString val = match.captured(1).split(".")[0].trimmed() + "%";
                if (i == 0 && lblSpeedCpu) lblSpeedCpu->setText(val);
                if (i == 1 && lblSpeedGpu) lblSpeedGpu->setText(val);
                i++;
            }
        }
    });
    timer->start(2000);

    mainWindow->show();
    return app.exec();
}