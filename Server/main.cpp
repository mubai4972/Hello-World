#include "WeQQ.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    WeQQ window;
    window.show();
    return app.exec();
}
