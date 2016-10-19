/*    Copyright 2012 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/bson/util/bson_extract.h"

#include <numeric>
#include <memory>

#include "mongo/db/jsobj.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

namespace
{
	class Canary
	{
		public:
			static constexpr size_t kSize = 16384;

		private:
			static volatile std::uint8_t *
			cloneBlock( volatile std::uint8_t *const p ) noexcept
			{
				auto rv= new std::uint8_t [ kSize ]();
				std::copy_n( p, kSize, rv );
				invariant( std::accumulate( rv, rv + kSize, std::size_t{} ) == std::accumulate( p, p + kSize, std::size_t{} ) );
				return rv;
			}

			const volatile std::size_t offloadChecksum1;
			const volatile std::uint8_t *const volatile offload1;

			const volatile std::size_t offloadChecksum2;
			const volatile std::uint8_t *const volatile offload2;

			const volatile std::size_t offloadChecksum3;
			const volatile std::uint8_t *const volatile offload3;

			const volatile std::size_t offloadChecksum4;
			const volatile std::uint8_t *const volatile offload4;

			volatile std::size_t offloadChecksumPost;
			const volatile std::uint8_t *volatile offloadPost;

			const volatile unsigned char* const volatile t;

			static constexpr uint8_t kBits= 0xCD;
			static constexpr size_t kChecksum= kSize * size_t( kBits );

			__attribute__(( __noinline__ ))
			void
			_verify() const noexcept
			{
				invariant( std::accumulate( &t[ 0 ], &t[ kSize ], size_t{} ) == kChecksum );
			}


		public:
			explicit
			Canary( volatile unsigned char *const i_t ) noexcept
					: offloadChecksum1( std::accumulate( i_t, i_t + kSize, std::size_t{} ) ), offload1( cloneBlock( i_t ) ),
					  offloadChecksum2( std::accumulate( i_t, i_t + kSize, std::size_t{} ) ), offload2( cloneBlock( i_t ) ),
					  offloadChecksum3( std::accumulate( i_t, i_t + kSize, std::size_t{} ) ), offload3( cloneBlock( i_t ) ),
					  offloadChecksum4( std::accumulate( i_t, i_t + kSize, std::size_t{} ) ), offload4( cloneBlock( i_t ) ),
					  t( i_t )
			{
				::memset( const_cast< unsigned char * >( t ), kBits, kSize );
				_verify();
				offloadChecksumPost= ( std::accumulate( i_t, i_t + kSize, std::size_t{} ) );
				offloadPost= cloneBlock( i_t );

				invariant( offloadChecksumPost == kChecksum );
				_verify();
				_verify();
			}

			~Canary() noexcept
			{
				_verify();
				_verify();
				const volatile bool ck1= std::accumulate( offload1, offload1 + kSize, std::size_t{} ) == offloadChecksum1;
				const volatile bool ck2= std::accumulate( offload2, offload2 + kSize, std::size_t{} ) == offloadChecksum2;
				const volatile bool ck3= std::accumulate( offload3, offload3 + kSize, std::size_t{} ) == offloadChecksum3;
				const volatile bool ck4= std::accumulate( offload4, offload4 + kSize, std::size_t{} ) == offloadChecksum4;
				const volatile bool ck1a= std::accumulate( offload1, offload1 + kSize, std::size_t{} ) == offloadChecksum1;
				const volatile bool ck2a= std::accumulate( offload2, offload2 + kSize, std::size_t{} ) == offloadChecksum2;
				const volatile bool ck3a= std::accumulate( offload3, offload3 + kSize, std::size_t{} ) == offloadChecksum3;
				const volatile bool ck4a= std::accumulate( offload4, offload4 + kSize, std::size_t{} ) == offloadChecksum4;

				const volatile bool ck1_2= offloadChecksum1 == offloadChecksum2;
				const volatile bool ck1_3= offloadChecksum1 == offloadChecksum3;
				const volatile bool ck1_4= offloadChecksum1 == offloadChecksum4;

				const volatile bool ck2_3= offloadChecksum2 == offloadChecksum3;
				const volatile bool ck2_4= offloadChecksum2 == offloadChecksum4;

				const volatile bool ck3_4= offloadChecksum3 == offloadChecksum4;


				invariant( ck1 );
				invariant( ck2 );
				invariant( ck3 );
				invariant( ck4 );
				invariant( ck1a );
				invariant( ck2a );
				invariant( ck3a );
				invariant( ck4a );

				invariant( ck1_2 );
				invariant( ck1_3 );
				invariant( ck1_4 );

				invariant( ck2_3 );
				invariant( ck2_4 );

				invariant( ck3_4 );

				delete offload4; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.
				delete offload3; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.
				delete offload2; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.
				delete offload1; // Doing a manual deletion here, because unique_ptr is too annoying to figure out how to get at the raw pointer during debug.

				_verify();

				invariant( offloadChecksumPost == kChecksum );

				invariant( std::accumulate( offloadPost, offloadPost + kSize, std::size_t{} ) == offloadChecksumPost );
				invariant( std::accumulate( offloadPost, offloadPost + kSize, std::size_t{} ) == kChecksum );

				delete offloadPost;

				_verify();
			}

	};
}  // namespace

Status bsonExtractField(const BSONObj& object, StringData fieldName, BSONElement* outElement) {

    volatile unsigned char* const cookie = static_cast<unsigned char *>(alloca(Canary::kSize));
    const Canary c(cookie);

    BSONElement element = object.getField(fieldName);
    if (element.eoo())
        return Status(ErrorCodes::NoSuchKey,
                      mongoutils::str::stream() << "Missing expected field \""
                                                << fieldName.toString()
                                                << "\"");
    *outElement = element;
    return Status::OK();
}

Status bsonExtractTypedField(const BSONObj& object,
                             StringData fieldName,
                             BSONType type,
                             BSONElement* outElement) {

    volatile unsigned char* const cookie = static_cast<unsigned char *>(alloca(Canary::kSize));
    const Canary c(cookie);

    Status status = bsonExtractField(object, fieldName, outElement);
    if (!status.isOK())
        return status;
    if (type != outElement->type()) {
        return Status(ErrorCodes::TypeMismatch,
                      mongoutils::str::stream() << "\"" << fieldName
                                                << "\" had the wrong type. Expected "
                                                << typeName(type)
                                                << ", found "
                                                << typeName(outElement->type()));
    }
    return Status::OK();
}

Status bsonExtractBooleanField(const BSONObj& object, StringData fieldName, bool* out) {
    BSONElement element;
    Status status = bsonExtractTypedField(object, fieldName, Bool, &element);
    if (!status.isOK())
        return status;
    *out = element.boolean();
    return Status::OK();
}

Status bsonExtractBooleanFieldWithDefault(const BSONObj& object,
                                          StringData fieldName,
                                          bool defaultValue,
                                          bool* out) {
    BSONElement value;
    Status status = bsonExtractField(object, fieldName, &value);
    if (status == ErrorCodes::NoSuchKey) {
        *out = defaultValue;
        return Status::OK();
    } else if (!status.isOK()) {
        return status;
    } else if (!value.isNumber() && !value.isBoolean()) {
        return Status(ErrorCodes::TypeMismatch,
                      mongoutils::str::stream() << "Expected boolean or number type for field \""
                                                << fieldName
                                                << "\", found "
                                                << typeName(value.type()));
    } else {
        *out = value.trueValue();
        return Status::OK();
    }
}

Status bsonExtractStringField(const BSONObj& object, StringData fieldName, std::string* out) {

    volatile unsigned char* const cookie = static_cast<unsigned char *>(alloca(Canary::kSize));
    const Canary c(cookie);

    const BSONObj* volatile vobj = &object;

    BSONElement element;
    Status status = bsonExtractTypedField(*vobj, fieldName, String, &element);
    if (!status.isOK())
        return status;
    *out = element.str();
    return Status::OK();
}

Status bsonExtractTimestampField(const BSONObj& object, StringData fieldName, Timestamp* out) {
    BSONElement element;
    Status status = bsonExtractTypedField(object, fieldName, bsonTimestamp, &element);
    if (!status.isOK())
        return status;
    *out = element.timestamp();
    return Status::OK();
}

Status bsonExtractOIDField(const BSONObj& object, StringData fieldName, OID* out) {
    BSONElement element;
    Status status = bsonExtractTypedField(object, fieldName, jstOID, &element);
    if (!status.isOK())
        return status;
    *out = element.OID();
    return Status::OK();
}

Status bsonExtractOIDFieldWithDefault(const BSONObj& object,
                                      StringData fieldName,
                                      const OID& defaultValue,
                                      OID* out) {
    Status status = bsonExtractOIDField(object, fieldName, out);
    if (status == ErrorCodes::NoSuchKey) {
        *out = defaultValue;
    } else if (!status.isOK()) {
        return status;
    }
    return Status::OK();
}

Status bsonExtractStringFieldWithDefault(const BSONObj& object,
                                         StringData fieldName,
                                         StringData defaultValue,
                                         std::string* out) {
    Status status = bsonExtractStringField(object, fieldName, out);
    if (status == ErrorCodes::NoSuchKey) {
        *out = defaultValue.toString();
    } else if (!status.isOK()) {
        return status;
    }
    return Status::OK();
}

Status bsonExtractIntegerField(const BSONObj& object, StringData fieldName, long long* out) {
    BSONElement value;
    Status status = bsonExtractField(object, fieldName, &value);
    if (!status.isOK())
        return status;
    if (!value.isNumber()) {
        return Status(ErrorCodes::TypeMismatch,
                      mongoutils::str::stream() << "Expected field \"" << fieldName
                                                << "\" to have numeric type, but found "
                                                << typeName(value.type()));
    }
    long long result = value.safeNumberLong();
    if (result != value.numberDouble()) {
        return Status(
            ErrorCodes::BadValue,
            mongoutils::str::stream() << "Expected field \"" << fieldName
                                      << "\" to have a value "
                                         "exactly representable as a 64-bit integer, but found "
                                      << value);
    }
    *out = result;
    return Status::OK();
}

Status bsonExtractIntegerFieldWithDefault(const BSONObj& object,
                                          StringData fieldName,
                                          long long defaultValue,
                                          long long* out) {
    Status status = bsonExtractIntegerField(object, fieldName, out);
    if (status == ErrorCodes::NoSuchKey) {
        *out = defaultValue;
        status = Status::OK();
    }
    return status;
}

Status bsonExtractIntegerFieldWithDefaultIf(const BSONObj& object,
                                            StringData fieldName,
                                            long long defaultValue,
                                            stdx::function<bool(long long)> pred,
                                            const std::string& predDescription,
                                            long long* out) {
    auto status = bsonExtractIntegerFieldWithDefault(object, fieldName, defaultValue, out);
    if (!status.isOK()) {
        return status;
    }
    if (!pred(*out)) {
        return Status(
            ErrorCodes::BadValue,
            mongoutils::str::stream() << "Invalid value in field \"" << fieldName << "\": " << *out
                                      << ": "
                                      << predDescription);
    }
    return Status::OK();
}

Status bsonExtractIntegerFieldWithDefaultIf(const BSONObj& object,
                                            StringData fieldName,
                                            long long defaultValue,
                                            stdx::function<bool(long long)> pred,
                                            long long* out) {
    return bsonExtractIntegerFieldWithDefaultIf(
        object, fieldName, defaultValue, pred, "constraint failed", out);
}

}  // namespace mongo
