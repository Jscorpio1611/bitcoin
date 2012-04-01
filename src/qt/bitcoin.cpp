/*
 * W.J. van der Laan 2011-2012
 */
#include "bitcoingui.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "guiutil.h"

#include "headers.h"
#include "init.h"
#include "qtipcserver.h"

#include <QApplication>
#include <QMessageBox>
#include <QTextCodec>
#include <QLocale>
#include <QTranslator>
#include <QSplashScreen>
#include <QLibraryInfo>

#include <boost/interprocess/ipc/message_queue.hpp>

using namespace boost;

#if defined(BITCOIN_NEED_QT_PLUGINS) && !defined(_BITCOIN_QT_PLUGINS_INCLUDED)
#define _BITCOIN_QT_PLUGINS_INCLUDED
#define __INSURE__
#include <QtPlugin>
Q_IMPORT_PLUGIN(qcncodecs)
Q_IMPORT_PLUGIN(qjpcodecs)
Q_IMPORT_PLUGIN(qtwcodecs)
Q_IMPORT_PLUGIN(qkrcodecs)
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#endif

// Need a global reference for the notifications to find the GUI
static BitcoinGUI *guiref;
static QSplashScreen *splashref;
static WalletModel *walletmodel;
static ClientModel *clientmodel;

int ThreadSafeMessageBox(const std::string& message, const std::string& caption, int style)
{
    // Message from network thread
    if(guiref)
    {
        bool modal = (style & wxMODAL);
        // in case of modal message, use blocking connection to wait for user to click OK
        QMetaObject::invokeMethod(guiref, "error",
                                   modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                                   Q_ARG(QString, QString::fromStdString(caption)),
                                   Q_ARG(QString, QString::fromStdString(message)),
                                   Q_ARG(bool, modal));
    }
    else
    {
        printf("%s: %s\n", caption.c_str(), message.c_str());
        fprintf(stderr, "%s: %s\n", caption.c_str(), message.c_str());
    }
    return 4;
}

bool ThreadSafeAskFee(int64 nFeeRequired, const std::string& strCaption)
{
    if(!guiref)
        return false;
    if(nFeeRequired < MIN_TX_FEE || nFeeRequired <= nTransactionFee || fDaemon)
        return true;
    bool payFee = false;

    QMetaObject::invokeMethod(guiref, "askFee", GUIUtil::blockingGUIThreadConnection(),
                               Q_ARG(qint64, nFeeRequired),
                               Q_ARG(bool*, &payFee));

    return payFee;
}

void ThreadSafeHandleURI(const std::string& strURI)
{
    if(!guiref)
        return;

    QMetaObject::invokeMethod(guiref, "handleURI", GUIUtil::blockingGUIThreadConnection(),
                               Q_ARG(QString, QString::fromStdString(strURI)));
}

void MainFrameRepaint()
{
    if(clientmodel)
        QMetaObject::invokeMethod(clientmodel, "update", Qt::QueuedConnection);
    if(walletmodel)
        QMetaObject::invokeMethod(walletmodel, "update", Qt::QueuedConnection);
}

void AddressBookRepaint()
{
    if(walletmodel)
        QMetaObject::invokeMethod(walletmodel, "updateAddressList", Qt::QueuedConnection);
}

void InitMessage(const std::string &message)
{
    if(splashref)
    {
        splashref->showMessage(QString::fromStdString(message), Qt::AlignBottom|Qt::AlignHCenter, QColor(255,255,200));
        QApplication::instance()->processEvents();
    }
}

void QueueShutdown()
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
}

/*
   Translate string to current locale using Qt.
 */
std::string _(const char* psz)
{
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
}

#ifdef WIN32
#define strncasecmp strnicmp
#endif
#ifndef BITCOIN_QT_TEST
int main(int argc, char *argv[])
{
#if !defined(MAC_OSX)
// TODO: implement qtipcserver.cpp for Mac

    // Do this early as we don't want to bother initializing if we are just calling IPC
    for (int i = 1; i < argc; i++)
    {
        // limit length of parsed URIs to max. size of message queue messages
        if (strlen(argv[i]) > 7 && strlen(argv[i]) <= BCQT_MQ_MAX_MESSAGE_SIZE && strncasecmp(argv[i], "bitcoin:", 8) == 0)
        {
            const char* pszURI = argv[i];
            try {
                interprocess::message_queue mq(interprocess::open_only, BCQT_MQ_NAME);
                if (mq.try_send(pszURI, strlen(pszURI), 0))
                    exit(0);
                else
                    break;
            }
            catch (interprocess::interprocess_exception &ex) {
                // don't log the "file not found" exception, because that's normal for
                // the first start of the first instance
                if (ex.get_error_code() != interprocess::not_found_error)
                {
                    printf("boost interprocess exception #%d: %s\n", ex.get_error_code(), ex.what());

                    // without this every clicked bitcoin: URI would show the datadir locked error in the case of an exception
                    // while the client is already running (perhaps this should be shown as message box?)
                    exit(0);
                }
                break;
            }
        }
    }
#endif

    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());

    Q_INIT_RESOURCE(bitcoin);
    QApplication app(argc, argv);

    // Command-line options take precedence:
    ParseParameters(argc, argv);

    // ... then bitcoin.conf:
    if (!ReadConfigFile(mapArgs, mapMultiArgs))
    {
        fprintf(stderr, "Error: Specified directory does not exist\n");
        return 1;
    }

    // Application identification (must be set before OptionsModel is initialized,
    // as it is used to locate QSettings)
    app.setOrganizationName("Bitcoin");
    app.setOrganizationDomain("bitcoin.org");
    if(GetBoolArg("-testnet")) // Separate UI settings for testnet
        app.setApplicationName("Bitcoin-Qt-testnet");
    else
        app.setApplicationName("Bitcoin-Qt");

    // ... then GUI settings:
    OptionsModel optionsModel;

    // Get desired locale ("en_US") from command line or system locale
    QString lang_territory = QString::fromStdString(GetArg("-lang", QLocale::system().name().toStdString()));
    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator
    QString lang = lang_territory;

    lang.truncate(lang_territory.lastIndexOf('_')); // "en"
    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;

    qtTranslatorBase.load(QLibraryInfo::location(QLibraryInfo::TranslationsPath) + "/qt_" + lang);
    if (!qtTranslatorBase.isEmpty())
        app.installTranslator(&qtTranslatorBase);

    qtTranslator.load(QLibraryInfo::location(QLibraryInfo::TranslationsPath) + "/qt_" + lang_territory);
    if (!qtTranslator.isEmpty())
        app.installTranslator(&qtTranslator);

    translatorBase.load(":/translations/"+lang);
    if (!translatorBase.isEmpty())
        app.installTranslator(&translatorBase);

    translator.load(":/translations/"+lang_territory);
    if (!translator.isEmpty())
        app.installTranslator(&translator);

    QSplashScreen splash(QPixmap(":/images/splash"), 0);
    if (GetBoolArg("-splash", true) && !GetBoolArg("-min"))
    {
        splash.show();
        splash.setAutoFillBackground(true);
        splashref = &splash;
    }

    app.processEvents();

    app.setQuitOnLastWindowClosed(false);

    try
    {
        BitcoinGUI window;
        guiref = &window;
        if(AppInit2(argc, argv))
        {
            {
                // Put this in a block, so that the Model objects are cleaned up before
                // calling Shutdown().

                optionsModel.Upgrade(); // Must be done after AppInit2

                if (splashref)
                    splash.finish(&window);

                ClientModel clientModel(&optionsModel);
                clientmodel = &clientModel;
                WalletModel walletModel(pwalletMain, &optionsModel);
                walletmodel = &walletModel;

                window.setClientModel(&clientModel);
                window.setWalletModel(&walletModel);

                // If -min option passed, start window minimized.
                if(GetBoolArg("-min"))
                {
                    window.showMinimized();
                }
                else
                {
                    window.show();
                }

                // Place this here as guiref has to be defined if we dont want to lose URIs
                ipcInit();

#if !defined(MAC_OSX)
// TODO: implement qtipcserver.cpp for Mac

                // Check for URI in argv
                for (int i = 1; i < argc; i++)
                {
                    // only bother with this if IPC is initialized
                    if (GlobalIpcState == IPC_INITIALIZED && strlen(argv[i]) > 7 && strlen(argv[i]) <= BCQT_MQ_MAX_MESSAGE_SIZE && strncasecmp(argv[i], "bitcoin:", 8) == 0)
                    {
                        const char* pszURI = argv[i];
                        try {
                            interprocess::message_queue mq(interprocess::open_only, BCQT_MQ_NAME);
                            mq.try_send(pszURI, strlen(pszURI), 0);
                        }
                        catch (interprocess::interprocess_exception &ex) {
                            printf("boost interprocess exception #%d: %s\n", ex.get_error_code(), ex.what());
                            break;
                        }
                    }
                }
#endif
                app.exec();

                window.setClientModel(0);
                window.setWalletModel(0);
                guiref = 0;
                clientmodel = 0;
                walletmodel = 0;
            }
            Shutdown(NULL);
        }
        else
        {
            return 1;
        }
    } catch (std::exception& e) {
        PrintException(&e, "Runaway exception");
    } catch (...) {
        PrintException(NULL, "Runaway exception");
    }
    return 0;
}
#endif // BITCOIN_QT_TEST
