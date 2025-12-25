#include "WeQQClient.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    WeQQClient window;
    window.show();
    return app.exec();
}
