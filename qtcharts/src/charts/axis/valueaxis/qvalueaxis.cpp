// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QtCharts/QValueAxis>
#include <private/qvalueaxis_p.h>
#include <private/chartvalueaxisx_p.h>
#include <private/chartvalueaxisy_p.h>
#include <private/abstractdomain_p.h>
#include <private/polarchartvalueaxisangular_p.h>
#include <private/polarchartvalueaxisradial_p.h>
#include <private/chartdataset_p.h>
#include <private/chartpresenter_p.h>
#include <private/charttheme_p.h>
#include <private/charthelpers_p.h>

QT_BEGIN_NAMESPACE
/*!
    \class QValueAxis
    \inmodule QtCharts
    \brief The QValueAxis class adds values to a chart's axes.

    A value axis can be set up to show an axis line with tick marks, grid lines, and shades.
    The values on the axis are drawn at the positions of tick marks.

    The following example code illustrates how to use the QValueAxis class:
    \code
    QChartView *chartView = new QChartView;
    QLineSeries *series = new QLineSeries;
    // ...
    chartView->chart()->addSeries(series);

    QValueAxis *axisX = new QValueAxis;
    axisX->setRange(10, 20.5);
    axisX->setTickCount(10);
    axisX->setLabelFormat("%.2f");
    chartView->chart()->setAxisX(axisX, series);
    \endcode
*/
/*!
    \qmltype ValueAxis
    \nativetype QValueAxis
    \inqmlmodule QtCharts

    \inherits AbstractAxis
    \brief Adds values to a chart's axes.

    The ValueAxis type can be set up to show an axis line with tick marks, grid lines, and shades.
    The values on the axis are drawn at the positions of tick marks.

    The following example code illustrates how to use the ValueAxis type:
    \code
        ChartView {
            ValueAxis {
                id: xAxis
                min: 0
                max: 10
            }
        // Add a few series...
        }
    \endcode
*/

/*!
  \property QValueAxis::min
  \brief The minimum value on the axis.

  When setting this property, the maximum value is adjusted if necessary, to ensure that
  the range remains valid.
*/
/*!
  \qmlproperty real ValueAxis::min
  The minimum value on the axis.

  When setting this property, the maximum value is adjusted if necessary, to ensure that
  the range remains valid.
*/

/*!
  \property QValueAxis::max
  \brief The maximum value on the axis.

  When setting this property, the minimum value is adjusted if necessary, to ensure that
  the range remains valid.
*/
/*!
  \qmlproperty real ValueAxis::max
  The maximum value on the axis.

  When setting this property, the minimum value is adjusted if necessary, to ensure that
  the range remains valid.
*/

/*!
  \property QValueAxis::tickCount
  \brief The number of tick marks on the axis. This indicates how many grid lines are drawn on the
  chart. The default value is 5, and the number cannot be less than 2.
*/
/*!
  \qmlproperty int ValueAxis::tickCount
  The number of tick marks on the axis. This indicates how many grid lines are drawn on the
  chart. The default value is 5, and the number cannot be less than 2.
*/

/*!
  \property QValueAxis::minorTickCount
  \brief The number of minor tick marks on the axis. This indicates how many grid lines are drawn
  between major ticks on the chart. Labels are not drawn for minor ticks. The default value is 0.
*/
/*!
  \qmlproperty int ValueAxis::minorTickCount
  The number of minor tick marks on the axis. This indicates how many grid lines are drawn
  between major ticks on the chart. Labels are not drawn for minor ticks. The default value is 0.
*/

/*!
  \property QValueAxis::tickAnchor
  \since 5.12
  \brief The base value where the dynamically placed tick marks and labels are started from.
*/
/*!
  \qmlproperty real ValueAxis::tickAnchor
  \since QtCharts 2.3
  The base value where the dynamically placed tick marks and labels are started from.
*/

/*!
  \property QValueAxis::tickInterval
  \since 5.12
  \brief The interval between dynamically placed tick marks and labels.
*/
/*!
  \qmlproperty real ValueAxis::tickInterval
  \since QtCharts 2.3
  The interval between dynamically placed tick marks and labels.
*/

/*!
 \enum QValueAxis::TickType

 This enum describes how the ticks and labels are positioned on the axis.

 \value TicksDynamic Ticks are placed according to tickAnchor and tickInterval values.
 \value TicksFixed Ticks are placed evenly across the axis range. The tickCount value
 specifies the number of ticks.
 */
/*!
  \property QValueAxis::tickType
  \since 5.12
  \brief The positioning method of tick and labels.
*/
/*!
  \qmlproperty enumeration ValueAxis::tickType
  \since QtCharts 2.3

  The positioning method of tick and labels.

 \value ValueAxis.TicksDynamic
        Ticks are placed according to tickAnchor and tickInterval values.
 \value ValueAxis.TicksFixed
        Ticks are placed evenly across the axis range. The tickCount value specifies the number of ticks.
*/

/*!
  \property QValueAxis::labelFormat
  \brief The label format of the axis.

  The format string supports the following conversion specifiers, length modifiers, and flags
  provided by \c printf() in the standard C++ library: d, i, o, x, X, f, F, e, E, g, G, c.

  If QChart::localizeNumbers is \c true, the supported specifiers are limited to:
  d, e, E, f, g, G, and i. Also, only the precision modifier is supported. The rest of the
  formatting comes from the default QLocale of the application.

  \sa QString::asprintf()
*/
/*!
  \qmlproperty string ValueAxis::labelFormat

  The format string supports the following conversion specifiers, length modifiers, and flags
  provided by \c printf() in the standard C++ library: d, i, o, x, X, f, F, e, E, g, G, c.

  If \l{ChartView::localizeNumbers}{ChartView.localizeNumbers} is \c true, the supported
  specifiers are limited to: d, e, E, f, g, G, and i. Also, only the precision modifier is
  supported. The rest of the formatting comes from the default QLocale of the application.

  \sa QString::asprintf()
*/

/*!
  \fn void QValueAxis::minChanged(qreal min)
  This signal is emitted when the minimum value of the axis, specified by \a min, changes.
*/

/*!
  \fn void QValueAxis::maxChanged(qreal max)
  This signal is emitted when the maximum value of the axis, specified by \a max, changes.
*/

/*!
  \fn void QValueAxis::tickCountChanged(int tickCount)
  This signal is emitted when the number of tick marks on the axis, specified by \a tickCount,
  changes.
*/

/*!
  \fn void QValueAxis::minorTickCountChanged(int minorTickCount)
  This signal is emitted when the number of minor tick marks on the axis, specified by
  \a minorTickCount, changes.
*/

/*!
  \fn void QValueAxis::rangeChanged(qreal min, qreal max)
  This signal is emitted when the minimum or maximum value of the axis, specified by \a min
  and \a max, changes.
*/

/*!
    \qmlsignal ValueAxis::rangeChanged(string min, string max)
    This signal is emitted when \a min or \a max value of the axis changes.

    The corresponding signal handler is \c onRangeChanged.
*/

/*!
  \fn void QValueAxis::labelFormatChanged(const QString &format)
  This signal is emitted when the \a format of axis labels changes.
*/

/*!
    Constructs an axis object that is a child of \a parent.
*/
QValueAxis::QValueAxis(QObject *parent) :
    QAbstractAxis(*new QValueAxisPrivate(this), parent)
{

}

/*!
    \internal
*/
QValueAxis::QValueAxis(QValueAxisPrivate &d, QObject *parent)
    : QAbstractAxis(d, parent)
{

}

/*!
    Destroys the object.
*/
QValueAxis::~QValueAxis()
{
    Q_D(QValueAxis);
    if (d->m_chart)
        d->m_chart->removeAxis(this);
}

void QValueAxis::setMin(qreal min)
{
    Q_D(QValueAxis);
    setRange(min, qMax(d->m_max, min));
}

qreal QValueAxis::min() const
{
    Q_D(const QValueAxis);
    return d->m_min;
}

void QValueAxis::setMax(qreal max)
{
    Q_D(QValueAxis);
    setRange(qMin(d->m_min, max), max);
}

qreal QValueAxis::max() const
{
    Q_D(const QValueAxis);
    return d->m_max;
}

/*!
  Sets the range from \a min to \a max on the axis.
  If \a min is greater than \a max, this function returns without making any changes.
*/
void QValueAxis::setRange(qreal min, qreal max)
{
    Q_D(QValueAxis);
    d->setRange(min,max);
}

void QValueAxis::setTickCount(int count)
{
    Q_D(QValueAxis);
    if (d->m_tickCount != count && count >= 2) {
        d->m_tickCount = count;
        emit tickCountChanged(count);
    }
}

int QValueAxis::tickCount() const
{
    Q_D(const QValueAxis);
    return d->m_tickCount;
}

void QValueAxis::setMinorTickCount(int count)
{
    Q_D(QValueAxis);
    if (d->m_minorTickCount != count && count >= 0) {
        d->m_minorTickCount = count;
        emit minorTickCountChanged(count);
    }
}

int QValueAxis::minorTickCount() const
{
    Q_D(const QValueAxis);
    return d->m_minorTickCount;
}


void QValueAxis::setTickInterval(qreal interval)
{
    Q_D(QValueAxis);
    if (d->m_tickInterval != interval) {
        d->m_tickInterval = interval;
        emit tickIntervalChanged(interval);
    }
}

qreal QValueAxis::tickInterval() const
{
    Q_D(const QValueAxis);
    return d->m_tickInterval;
}

void QValueAxis::setTickAnchor(qreal anchor)
{
    Q_D(QValueAxis);
    if (d->m_tickAnchor != anchor) {
        d->m_tickAnchor = anchor;
        emit tickAnchorChanged(anchor);
    }
}

qreal QValueAxis::tickAnchor() const
{
    Q_D(const QValueAxis);
    return d->m_tickAnchor;
}

void QValueAxis::setTickType(QValueAxis::TickType type)
{
    Q_D(QValueAxis);
    if (d->m_tickType != type) {
        d->m_tickType = type;
        emit tickTypeChanged(type);
    }
}

QValueAxis::TickType QValueAxis::tickType() const
{
    Q_D(const QValueAxis);
    return d->m_tickType;
}

void QValueAxis::setLabelFormat(const QString &format)
{
    Q_D(QValueAxis);
    d->m_format = format;
    emit labelFormatChanged(format);
}

QString QValueAxis::labelFormat() const
{
    Q_D(const QValueAxis);
    return d->m_format;
}

/*!
  Returns the type of the axis.
*/
QAbstractAxis::AxisType QValueAxis::type() const
{
    return AxisTypeValue;
}

/*!
    \qmlmethod ValueAxis::applyNiceNumbers()
    Modifies the current range and number of tick marks on the axis to look
    \e nice. The algorithm considers numbers that can be expressed as a form of
    1*10^n, 2* 10^n, or 5*10^n to be nice numbers. These numbers are used for
    setting spacing for the tick marks.
*/

/*!
    Modifies the current range and number of tick marks on the axis to look \e nice. The algorithm
    considers numbers that can be expressed as a form of 1*10^n, 2* 10^n, or 5*10^n to be
    nice numbers. These numbers are used for setting spacing for the tick marks.

    \sa setRange(), setTickCount()
*/
void QValueAxis::applyNiceNumbers()
{
    Q_D(QValueAxis);
    if(d->m_applying) return;
    qreal min = d->m_min;
    qreal max = d->m_max;
    int ticks = d->m_tickCount;
    AbstractDomain::looseNiceNumbers(min,max,ticks);
    d->m_applying=true;
    d->setRange(min,max);
    setTickCount(ticks);
    d->m_applying=false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

QValueAxisPrivate::QValueAxisPrivate(QValueAxis *q)
    : QAbstractAxisPrivate(q),
      m_min(0),
      m_max(0),
      m_tickCount(5),
      m_minorTickCount(0),
      m_format(),
      m_applying(false),
      m_tickInterval(0.0),
      m_tickAnchor(0.0),
      m_tickType(QValueAxis::TicksFixed)
{

}

QValueAxisPrivate::~QValueAxisPrivate()
{

}

void QValueAxisPrivate::setMin(const QVariant &min)
{
    Q_Q(QValueAxis);
    bool ok;
    qreal value = min.toReal(&ok);
    if (ok)
        q->setMin(value);
}

void QValueAxisPrivate::setMax(const QVariant &max)
{
    Q_Q(QValueAxis);
    bool ok;
    qreal value = max.toReal(&ok);
    if (ok)
        q->setMax(value);
}

void QValueAxisPrivate::setRange(const QVariant &min, const QVariant &max)
{
    Q_Q(QValueAxis);
    bool ok1;
    bool ok2;
    qreal value1 = min.toReal(&ok1);
    qreal value2 = max.toReal(&ok2);
    if (ok1 && ok2)
        q->setRange(value1, value2);
}

void QValueAxisPrivate::setRange(qreal min, qreal max)
{
    Q_Q(QValueAxis);
    bool changed = false;

    if (min > max)
        return;

    if (!isValidValue(min, max)) {
        qWarning() << "Attempting to set invalid range for value axis: ["
                   << min << " - " << max << "]";
        return;
    }

    if (m_min != min) {
        m_min = min;
        changed = true;
        emit q->minChanged(min);
    }

    if (m_max != max) {
        m_max = max;
        changed = true;
        emit q->maxChanged(max);
    }

    if (changed) {
        emit rangeChanged(min,max);
        emit q->rangeChanged(min, max);
    }
}

void QValueAxisPrivate::initializeGraphics(QGraphicsItem *parent)
{
    Q_Q(QValueAxis);
    ChartAxisElement *axis(0);

    if (m_chart->chartType() == QChart::ChartTypeCartesian) {
        if (orientation() == Qt::Vertical)
            axis = new ChartValueAxisY(q,parent);
        if (orientation() == Qt::Horizontal)
            axis = new ChartValueAxisX(q,parent);
        axis->setLabelsEditable(q->labelsEditable());
    }

    if (m_chart->chartType() == QChart::ChartTypePolar) {
        if (orientation() == Qt::Vertical)
            axis = new PolarChartValueAxisRadial(q, parent);
        if (orientation() == Qt::Horizontal)
            axis = new PolarChartValueAxisAngular(q, parent);
    }

    m_item.reset(axis);
    QAbstractAxisPrivate::initializeGraphics(parent);
}


void QValueAxisPrivate::initializeDomain(AbstractDomain *domain)
{
    if (orientation() == Qt::Vertical) {
        if (!qFuzzyIsNull(m_max - m_min))
            domain->setRangeY(m_min, m_max);
        else
            setRange(domain->minY(), domain->maxY());
    }
    if (orientation() == Qt::Horizontal) {
        if (!qFuzzyIsNull(m_max - m_min))
            domain->setRangeX(m_min, m_max);
        else
            setRange(domain->minX(), domain->maxX());
    }
}

QT_END_NAMESPACE

#include "moc_qvalueaxis.cpp"
#include "moc_qvalueaxis_p.cpp"
