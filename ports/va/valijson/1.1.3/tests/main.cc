#include <valijson/adapters/std_string_adapter.hpp>
#include <valijson/constraints/concrete_constraints.hpp>
#include <valijson/schema.hpp>
#include <valijson/validation_results.hpp>
#include <valijson/validator.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace c = valijson::constraints;
using valijson::Schema;
using valijson::Subschema;
using valijson::ValidationResults;
using valijson::Validator;
using valijson::adapters::StdStringAdapter;
using valijson::adapters::StdStringFrozenValue;

static int failures = 0;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

static bool valid(const Schema &schema, const std::string &value,
                  Validator::TypeCheckingMode mode = Validator::kStrongTypes) {
  Validator validator(mode);
  return validator.validate(schema, StdStringAdapter(value), nullptr);
}

// Validates `value` and reports whether any error description equals `want`.
static bool fails_with(const Schema &schema, const std::string &value,
                       const std::string &want) {
  Validator validator;
  ValidationResults results;
  if (validator.validate(schema, StdStringAdapter(value), &results)) {
    return false;
  }
  for (const auto &error : results) {
    if (error.description == want) {
      return true;
    }
  }
  return false;
}

int main() {
  // A username: a string of 3 to 8 characters (counted as UTF-8 code
  // points), lower-case ASCII letters and digits only.
  Schema user;
  c::TypeConstraint type;
  type.addNamedType(c::TypeConstraint::kString);
  user.addConstraint(type);
  c::MinLengthConstraint min_length;
  min_length.setMinLength(3);
  user.addConstraint(min_length);
  c::MaxLengthConstraint max_length;
  max_length.setMaxLength(8);
  user.addConstraint(max_length);
  c::PatternConstraint pattern;
  pattern.setPattern(std::string("^[a-z0-9]+$"));
  user.addConstraint(pattern);

  check(valid(user, "alice42"), "alice42 is a valid username");
  check(fails_with(user, "al", "String should be no fewer than 3 characters in length."),
        "al is too short");
  check(fails_with(user, "alexander9", "String should be no more than 8 characters in length."),
        "alexander9 is too long");
  check(fails_with(user, "Alice", "Failed to match regex specified by 'pattern' constraint."),
        "Alice breaks the pattern");

  // Length counts code points, not bytes: "\xc3\xa9t\xc3\xa9" ("été") is
  // three code points in five bytes.
  Schema short_text;
  c::MaxLengthConstraint three;
  three.setMaxLength(3);
  short_text.addConstraint(three);
  check(valid(short_text, "\xc3\xa9t\xc3\xa9"), "UTF-8 text measured in code points");
  check(!valid(short_text, "\xc3\xa9t\xc3\xa9s"), "four code points exceed maxLength 3");

  // enum, const and format.
  Schema colour;
  c::EnumConstraint colours;
  colours.addValue(StdStringFrozenValue("red"));
  colours.addValue(StdStringAdapter("green"));
  colour.addConstraint(colours);
  check(valid(colour, "red") && valid(colour, "green"), "enum members validate");
  check(fails_with(colour, "blue", "Failed to match against any enum values."),
        "blue is not an enum member");

  Schema exact;
  c::ConstConstraint version;
  version.setValue(StdStringAdapter("v1"));
  exact.addConstraint(version);
  check(valid(exact, "v1") && !valid(exact, "v2"), "const matches only v1");

  Schema date;
  c::FormatConstraint date_format;
  date_format.setFormat("date");
  date.addConstraint(date_format);
  check(valid(date, "2024-02-29"), "2024-02-29 is a leap day");
  check(!valid(date, "2023-02-29"), "2023-02-29 does not exist");
  check(fails_with(date, "29/02/2024", "String should be a valid date"),
        "29/02/2024 is not an RFC 3339 date");

  // Subschemas combined with anyOf, oneOf and not.
  Schema combined;
  const Subschema *digits = combined.createSubschema();
  c::PatternConstraint digits_pattern;
  digits_pattern.setPattern(std::string("^[0-9]+$"));
  combined.addConstraintToSubschema(digits_pattern, digits);
  const Subschema *short_value = combined.createSubschema();
  c::MaxLengthConstraint two;
  two.setMaxLength(2);
  combined.addConstraintToSubschema(two, short_value);

  Schema any_of;
  const Subschema *any_digits = any_of.createSubschema();
  any_of.addConstraintToSubschema(digits_pattern, any_digits);
  const Subschema *any_short = any_of.createSubschema();
  any_of.addConstraintToSubschema(two, any_short);
  c::AnyOfConstraint either;
  either.addSubschema(any_digits);
  either.addSubschema(any_short);
  any_of.addConstraint(either);
  check(valid(any_of, "12345") && valid(any_of, "ab"), "anyOf accepts either branch");
  check(fails_with(any_of, "abc",
                   "Failed to validate against any schemas allowed by anyOf constraint."),
        "anyOf rejects a value matching neither branch");

  c::OneOfConstraint exactly_one;
  exactly_one.addSubschema(digits);
  exactly_one.addSubschema(short_value);
  combined.addConstraint(exactly_one);
  check(valid(combined, "123") && valid(combined, "ab"), "oneOf accepts one match");
  check(fails_with(combined, "12", "Failed to validate against exactly one child schema."),
        "oneOf rejects a value matching both branches");

  // pattern is an unanchored search, oneOf also fails on no match, and
  // minLength counts code points too.
  Schema contains_digit;
  c::PatternConstraint some_digit;
  some_digit.setPattern(std::string("[0-9]"));
  contains_digit.addConstraint(some_digit);
  check(valid(contains_digit, "ab1c") && !valid(contains_digit, "abc"), "pattern is a search");
  check(fails_with(combined, "abc",
                   "Failed to validate against any child schemas allowed by oneOf constraint."),
        "oneOf rejects a value matching no branch");
  Schema long_text;
  c::MinLengthConstraint four;
  four.setMinLength(4);
  long_text.addConstraint(four);
  check(!valid(long_text, "\xc3\xa9t\xc3\xa9"), "minLength counts code points");

  Schema negated;
  const Subschema *forbidden = negated.createSubschema();
  c::EnumConstraint reserved;
  reserved.addValue(StdStringAdapter("root"));
  negated.addConstraintToSubschema(reserved, forbidden);
  c::NotConstraint not_reserved;
  not_reserved.setSubschema(forbidden);
  negated.addConstraint(not_reserved);
  check(valid(negated, "alice") && !valid(negated, "root"), "not rejects the reserved name");

  // Weak typing lets a string that parses as an integer satisfy "integer".
  Schema integer;
  c::TypeConstraint integer_type;
  integer_type.addNamedType(c::TypeConstraint::kInteger);
  integer.addConstraint(integer_type);
  check(!valid(integer, "42"), "strong typing keeps \"42\" a string");
  check(valid(integer, "42", Validator::kWeakTypes), "weak typing reads \"42\" as an integer");
  check(!valid(integer, "4.2", Validator::kWeakTypes), "4.2 is not an integer");
  Schema number;
  c::TypeConstraint number_type;
  number_type.addNamedType(c::TypeConstraint::kNumber);
  number.addConstraint(number_type);
  check(valid(number, "-4.25e1", Validator::kWeakTypes), "weak typing reads -4.25e1 as a number");
  check(!valid(number, "4.2x", Validator::kWeakTypes), "4.2x is not a number");

  // VALIJSON_USE_EXCEPTIONS=1, the interface define, reports misuse as an
  // exception instead of aborting.
  bool threw = false;
  try {
    (void)user.getDescription();
  } catch (const std::runtime_error &e) {
    threw = std::string(e.what()) == "Schema does not have a description";
  }
  check(threw, "a missing description throws std::runtime_error");
  user.setSubschemaDescription(user.root(), "user name");
  check(user.hasDescription() && user.getDescription() == "user name",
        "description is stored on the root");

  std::printf("valijson (C++): %s\n", failures == 0 ? "ok" : "FAILED");
  return failures == 0 ? 0 : 1;
}
