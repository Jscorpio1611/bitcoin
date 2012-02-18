#include "guiutil.h"
#include "bitcoinaddressvalidator.h"
#include "walletmodel.h"
#include "bitcoinunits.h"

#include "headers.h"

#include <QString>
#include <QDateTime>
#include <QDoubleValidator>
#include <QFont>
#include <QLineEdit>
#include <QUrl>
#include <QTextDocument> // For Qt::escape
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>

QString GUIUtil::dateTimeStr(qint64 nTime)
{
    return dateTimeStr(QDateTime::fromTime_t((qint32)nTime));
}

QString GUIUtil::dateTimeStr(const QDateTime &date)
{
    return date.date().toString(Qt::SystemLocaleShortDate) + QString(" ") + date.toString("hh:mm");
}

QFont GUIUtil::bitcoinAddressFont()
{
    QFont font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    return font;
}

void GUIUtil::setupAddressWidget(QLineEdit *widget, QWidget *parent)
{
    widget->setMaxLength(BitcoinAddressValidator::MaxAddressLength);
    widget->setValidator(new BitcoinAddressValidator(parent));
    widget->setFont(bitcoinAddressFont());
}

void GUIUtil::setupAmountWidget(QLineEdit *widget, QWidget *parent)
{
    QDoubleValidator *amountValidator = new QDoubleValidator(parent);
    amountValidator->setDecimals(8);
    amountValidator->setBottom(0.0);
    widget->setValidator(amountValidator);
    widget->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
}

static
int64 parseNumber(std::string strNumber, bool fHex)
{
    int64 nAmount = 0;
    std::istringstream stream(strNumber);
    stream >> (fHex ? std::hex : std::dec) >> nAmount;
    return nAmount;
}

int64 URIParseAmount(std::string strAmount)
{
    int64 nAmount = 0;
    bool fHex = false;
    if (strAmount[0] == 'x' || strAmount[0] == 'X')
    {
        fHex = true;
        strAmount = strAmount.substr(1);
    }
    size_t nPosX = strAmount.find('X', 1);
    if (nPosX == std::string::npos)
        nPosX = strAmount.find('x', 1);
    int nExponent = 0;
    if (nPosX != std::string::npos)
        nExponent = parseNumber(strAmount.substr(nPosX + 1), fHex);
    else
    {
        // Non-compliant URI, assume standard units
        nExponent = fHex ? 4 : 8;
        nPosX = strAmount.size();
    }
    size_t nPosP = strAmount.find('.');
    size_t nFractionLen = 0;
    if (nPosP == std::string::npos)
        nPosP = nPosX;
    else
        nFractionLen = (nPosX - nPosP) - 1;
    nExponent -= nFractionLen;
    strAmount = strAmount.substr(0, nPosP) + (nFractionLen ? strAmount.substr(nPosP + 1, nFractionLen) : "");
    if (nExponent > 0)
        strAmount.append(nExponent, '0');
    else
    if (nExponent < 0)
        // WTF? truncate I guess
        strAmount = strAmount.substr(0, strAmount.size() + nExponent);
    nAmount = parseNumber(strAmount, fHex);
    return nAmount;
}

bool GUIUtil::parseBitcoinURL(const QUrl &url, SendCoinsRecipient *out)
{
    if(url.scheme() != QString("bitcoin"))
        return false;

    SendCoinsRecipient rv;
    rv.address = url.path();
    rv.amount = 0;
    QList<QPair<QString, QString> > items = url.queryItems();
    for (QList<QPair<QString, QString> >::iterator i = items.begin(); i != items.end(); i++)
    {
        bool fShouldReturnFalse = false;
        if (i->first.startsWith("req-"))
        {
            i->first.remove(0, 4);
            fShouldReturnFalse = true;
        }

        if (i->first == "label")
        {
            rv.label = i->second;
            fShouldReturnFalse = false;
        }
        else if (i->first == "amount")
        {
            if(!i->second.isEmpty())
            {
                rv.amount = URIParseAmount((i->second).toStdString());
            }
            fShouldReturnFalse = false;
        }

        if (fShouldReturnFalse)
            return false;
    }
    if(out)
    {
        *out = rv;
    }
    return true;
}

bool GUIUtil::parseBitcoinURL(QString url, SendCoinsRecipient *out)
{
    // Convert bitcoin:// to bitcoin:
    //
    //    Cannot handle this later, because bitcoin:// will cause Qt to see the part after // as host,
    //    which will lowercase it (and thus invalidate the address).
    if(url.startsWith("bitcoin://"))
    {
        url.replace(0, 10, "bitcoin:");
    }
    QUrl urlInstance(url);
    return parseBitcoinURL(urlInstance, out);
}

QString GUIUtil::HtmlEscape(const QString& str, bool fMultiLine)
{
    QString escaped = Qt::escape(str);
    if(fMultiLine)
    {
        escaped = escaped.replace("\n", "<br>\n");
    }
    return escaped;
}

QString GUIUtil::HtmlEscape(const std::string& str, bool fMultiLine)
{
    return HtmlEscape(QString::fromStdString(str), fMultiLine);
}

void GUIUtil::copyEntryData(QAbstractItemView *view, int column, int role)
{
    if(!view || !view->selectionModel())
        return;
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if(!selection.isEmpty())
    {
        // Copy first item
        QApplication::clipboard()->setText(selection.at(0).data(role).toString());
    }
}
