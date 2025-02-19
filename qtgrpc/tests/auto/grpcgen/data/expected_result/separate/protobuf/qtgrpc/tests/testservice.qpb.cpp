/* This file is autogenerated. DO NOT CHANGE. All changes will be lost */


#include "qtgrpc/tests/testservice.qpb.h"

#include <QtProtobuf/qprotobufregistration.h>

#include <cmath>

namespace qtgrpc::tests {

class SimpleStringMessage_QtProtobufData : public QSharedData
{
public:
    SimpleStringMessage_QtProtobufData()
        : QSharedData()
    {
    }

    SimpleStringMessage_QtProtobufData(const SimpleStringMessage_QtProtobufData &other)
        : QSharedData(other),
          m_testFieldString(other.m_testFieldString)
    {
    }

    QString m_testFieldString;
};

SimpleStringMessage::~SimpleStringMessage() = default;

static constexpr struct {
    QtProtobufPrivate::QProtobufPropertyOrdering::Data data;
    const std::array<uint, 5> qt_protobuf_SimpleStringMessage_uint_data;
    const char qt_protobuf_SimpleStringMessage_char_data[50];
} qt_protobuf_SimpleStringMessage_metadata {
    // data
    {
        0, /* = version */
        1, /* = num fields */
        2, /* = field number offset */
        3, /* = property index offset */
        4, /* = field flags offset */
        32, /* = message full name length */
    },
    // uint_data
    {
        // JSON name offsets:
        33, /* = testFieldString */
        49, /* = end-of-string-marker */
        // Field numbers:
        6, /* = testFieldString */
        // Property indices:
        0, /* = testFieldString */
        // Field flags:
        uint(QtProtobufPrivate::FieldFlag::NoFlags), /* = testFieldString */
    },
    // char_data
    /* metadata char_data: */
    "qtgrpc.tests.SimpleStringMessage\0" /* = full message name */
    /* field char_data: */
    "testFieldString\0"
};

const QtProtobufPrivate::QProtobufPropertyOrdering SimpleStringMessage::staticPropertyOrdering = {
    &qt_protobuf_SimpleStringMessage_metadata.data
};

void SimpleStringMessage::registerTypes()
{
    qRegisterMetaType<SimpleStringMessage>();
    qRegisterMetaType<SimpleStringMessageRepeated>();
}

SimpleStringMessage::SimpleStringMessage()
    : QProtobufMessage(&SimpleStringMessage::staticMetaObject, &SimpleStringMessage::staticPropertyOrdering),
      dptr(new SimpleStringMessage_QtProtobufData)
{
}

SimpleStringMessage::SimpleStringMessage(const SimpleStringMessage &other)
    = default;
SimpleStringMessage &SimpleStringMessage::operator =(const SimpleStringMessage &other)
{
    SimpleStringMessage temp(other);
    swap(temp);
    return *this;
}
SimpleStringMessage::SimpleStringMessage(SimpleStringMessage &&other) noexcept
    = default;
bool comparesEqual(const SimpleStringMessage &lhs, const SimpleStringMessage &rhs) noexcept
{
    return operator ==(static_cast<const QProtobufMessage&>(lhs),
                       static_cast<const QProtobufMessage&>(rhs))
        && lhs.dptr->m_testFieldString == rhs.dptr->m_testFieldString;
}

const QString &SimpleStringMessage::testFieldString() const &
{
    return dptr->m_testFieldString;
}

void SimpleStringMessage::setTestFieldString(const QString &testFieldString)
{
    if (dptr->m_testFieldString != testFieldString) {
        dptr.detach();
        dptr->m_testFieldString = testFieldString;
    }
}

void SimpleStringMessage::setTestFieldString(QString &&testFieldString)
{
    if (dptr->m_testFieldString != testFieldString) {
        dptr.detach();
        dptr->m_testFieldString = std::move(testFieldString);
    }
}


class SimpleIntMessage_QtProtobufData : public QSharedData
{
public:
    SimpleIntMessage_QtProtobufData()
        : QSharedData(),
          m_testField(0)
    {
    }

    SimpleIntMessage_QtProtobufData(const SimpleIntMessage_QtProtobufData &other)
        : QSharedData(other),
          m_testField(other.m_testField)
    {
    }

    QtProtobuf::sint32 m_testField;
};

SimpleIntMessage::~SimpleIntMessage() = default;

static constexpr struct {
    QtProtobufPrivate::QProtobufPropertyOrdering::Data data;
    const std::array<uint, 5> qt_protobuf_SimpleIntMessage_uint_data;
    const char qt_protobuf_SimpleIntMessage_char_data[41];
} qt_protobuf_SimpleIntMessage_metadata {
    // data
    {
        0, /* = version */
        1, /* = num fields */
        2, /* = field number offset */
        3, /* = property index offset */
        4, /* = field flags offset */
        29, /* = message full name length */
    },
    // uint_data
    {
        // JSON name offsets:
        30, /* = testField */
        40, /* = end-of-string-marker */
        // Field numbers:
        1, /* = testField */
        // Property indices:
        0, /* = testField */
        // Field flags:
        uint(QtProtobufPrivate::FieldFlag::NoFlags), /* = testField */
    },
    // char_data
    /* metadata char_data: */
    "qtgrpc.tests.SimpleIntMessage\0" /* = full message name */
    /* field char_data: */
    "testField\0"
};

const QtProtobufPrivate::QProtobufPropertyOrdering SimpleIntMessage::staticPropertyOrdering = {
    &qt_protobuf_SimpleIntMessage_metadata.data
};

void SimpleIntMessage::registerTypes()
{
    qRegisterMetaType<SimpleIntMessage>();
    qRegisterMetaType<SimpleIntMessageRepeated>();
}

SimpleIntMessage::SimpleIntMessage()
    : QProtobufMessage(&SimpleIntMessage::staticMetaObject, &SimpleIntMessage::staticPropertyOrdering),
      dptr(new SimpleIntMessage_QtProtobufData)
{
}

SimpleIntMessage::SimpleIntMessage(const SimpleIntMessage &other)
    = default;
SimpleIntMessage &SimpleIntMessage::operator =(const SimpleIntMessage &other)
{
    SimpleIntMessage temp(other);
    swap(temp);
    return *this;
}
SimpleIntMessage::SimpleIntMessage(SimpleIntMessage &&other) noexcept
    = default;
bool comparesEqual(const SimpleIntMessage &lhs, const SimpleIntMessage &rhs) noexcept
{
    return operator ==(static_cast<const QProtobufMessage&>(lhs),
                       static_cast<const QProtobufMessage&>(rhs))
        && lhs.dptr->m_testField == rhs.dptr->m_testField;
}

QtProtobuf::sint32 SimpleIntMessage::testField() const
{
    return dptr->m_testField;
}

void SimpleIntMessage::setTestField(QtProtobuf::sint32 testField)
{
    if (dptr->m_testField != testField) {
        dptr.detach();
        dptr->m_testField = testField;
    }
}


class BlobMessage_QtProtobufData : public QSharedData
{
public:
    BlobMessage_QtProtobufData()
        : QSharedData()
    {
    }

    BlobMessage_QtProtobufData(const BlobMessage_QtProtobufData &other)
        : QSharedData(other),
          m_testBytes(other.m_testBytes)
    {
    }

    QByteArray m_testBytes;
};

BlobMessage::~BlobMessage() = default;

static constexpr struct {
    QtProtobufPrivate::QProtobufPropertyOrdering::Data data;
    const std::array<uint, 5> qt_protobuf_BlobMessage_uint_data;
    const char qt_protobuf_BlobMessage_char_data[36];
} qt_protobuf_BlobMessage_metadata {
    // data
    {
        0, /* = version */
        1, /* = num fields */
        2, /* = field number offset */
        3, /* = property index offset */
        4, /* = field flags offset */
        24, /* = message full name length */
    },
    // uint_data
    {
        // JSON name offsets:
        25, /* = testBytes */
        35, /* = end-of-string-marker */
        // Field numbers:
        1, /* = testBytes */
        // Property indices:
        0, /* = testBytes */
        // Field flags:
        uint(QtProtobufPrivate::FieldFlag::NoFlags), /* = testBytes */
    },
    // char_data
    /* metadata char_data: */
    "qtgrpc.tests.BlobMessage\0" /* = full message name */
    /* field char_data: */
    "testBytes\0"
};

const QtProtobufPrivate::QProtobufPropertyOrdering BlobMessage::staticPropertyOrdering = {
    &qt_protobuf_BlobMessage_metadata.data
};

void BlobMessage::registerTypes()
{
    qRegisterMetaType<BlobMessage>();
    qRegisterMetaType<BlobMessageRepeated>();
}

BlobMessage::BlobMessage()
    : QProtobufMessage(&BlobMessage::staticMetaObject, &BlobMessage::staticPropertyOrdering),
      dptr(new BlobMessage_QtProtobufData)
{
}

BlobMessage::BlobMessage(const BlobMessage &other)
    = default;
BlobMessage &BlobMessage::operator =(const BlobMessage &other)
{
    BlobMessage temp(other);
    swap(temp);
    return *this;
}
BlobMessage::BlobMessage(BlobMessage &&other) noexcept
    = default;
bool comparesEqual(const BlobMessage &lhs, const BlobMessage &rhs) noexcept
{
    return operator ==(static_cast<const QProtobufMessage&>(lhs),
                       static_cast<const QProtobufMessage&>(rhs))
        && lhs.dptr->m_testBytes == rhs.dptr->m_testBytes;
}

const QByteArray &BlobMessage::testBytes() const &
{
    return dptr->m_testBytes;
}

void BlobMessage::setTestBytes(const QByteArray &testBytes)
{
    if (dptr->m_testBytes != testBytes) {
        dptr.detach();
        dptr->m_testBytes = testBytes;
    }
}

void BlobMessage::setTestBytes(QByteArray &&testBytes)
{
    if (dptr->m_testBytes != testBytes) {
        dptr.detach();
        dptr->m_testBytes = std::move(testBytes);
    }
}

} // namespace qtgrpc::tests

#include "moc_testservice.qpb.cpp"
