#include <tao/json.hpp>
#include <tao/json/cbor.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

static int failures = 0;

#define CHECK( cond ) \
   do { \
      if( !( cond ) ) { \
         std::cerr << "FAILED: " #cond " (line " << __LINE__ << ")\n"; \
         ++failures; \
      } \
   } while( false )

int main()
{
   const auto v = tao::json::from_string( R"({"b":"x","a":[1,2.5,true,null]})" );

   CHECK( v.is_object() );
   const auto& a = v.at( "a" );
   CHECK( a.get_array().size() == 4 );
   CHECK( a[ 0 ].as< std::int64_t >() == 1 );
   CHECK( a[ 1 ].get_double() == 2.5 );
   CHECK( a[ 2 ].get_boolean() );
   CHECK( a[ 3 ].is_null() );
   CHECK( v.at( "b" ).get_string() == "x" );

   // Objects are std::map, so keys come out sorted.
   CHECK( tao::json::to_string( v ) == R"({"a":[1,2.5,true,null],"b":"x"})" );

   const auto bytes = tao::json::cbor::to_string( v );
   CHECK( !bytes.empty() );
   CHECK( tao::json::cbor::from_string( bytes ) == v );

   // Shortest round trip of doubles, through the bundled ryu and double-conversion.
   const auto d = tao::json::from_string( "[0.1,1e300,-1.7976931348623157e308]" );
   CHECK( tao::json::to_string( d ) == "[0.1,1e300,-1.7976931348623157e308]" );
   CHECK( d[ 1 ].get_double() == 1e300 );

   bool threw = false;
   try {
      (void)tao::json::from_string( "[1," );
   }
   catch( const tao::pegtl::parse_error& ) {
      threw = true;
   }
   CHECK( threw );

   return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
