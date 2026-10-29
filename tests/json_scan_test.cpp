#include "setup/json_scan.h"
#include "test_util.h"

TEST(release_json) {
  auto r = gow3::setup::ParseRelease(R"({"tag_name":"v0.3.0","prerelease":false,"body":"Fixes \"x\"\nline2",
    "assets":[{"name":"gow3-recomp-0.3.0-win64.zip","uploader":{"name":"n"}},{"name":"gow3-recomp-0.3.0-win64.zip.sha256"}]})");
  CHECK(r && r->tag == "v0.3.0" && !r->prerelease && r->body == "Fixes \"x\"\nline2");
  CHECK(r->asset_names.size() == 2);
}
TEST(release_json_bad) { CHECK(!gow3::setup::ParseRelease("{\"tag_name\": ")); CHECK(!gow3::setup::ParseRelease("[]")); }

TEST(release_json_unicode_escapes) {
  // \u00e9 (2-byte UTF-8), \u2013 (3-byte), a surrogate pair for U+1F600 (4-byte), escaped / and \.
  auto r = gow3::setup::ParseRelease(
      R"({"tag_name":"v1.0.0","body":"caf\u00e9 \u2013 \ud83d\ude00 a\/b\\c\t","prerelease":true,"assets":[]})");
  CHECK(r && r->body == "caf\xC3\xA9 \xE2\x80\x93 \xF0\x9F\x98\x80 a/b\\c\t" && r->prerelease);
  CHECK(r && r->asset_names.empty());
  // A lone or reversed surrogate, a short or non-hex \u and an unknown escape are malformed.
  CHECK(!gow3::setup::ParseRelease(R"({"body":"\ud83d"})"));
  CHECK(!gow3::setup::ParseRelease(R"({"body":"\ude00\ud83d"})"));
  CHECK(!gow3::setup::ParseRelease(R"({"body":"\u12"})"));
  CHECK(!gow3::setup::ParseRelease(R"({"body":"\u12G4"})"));
  CHECK(!gow3::setup::ParseRelease(R"({"body":"\q"})"));
  CHECK(!gow3::setup::ParseRelease("{\"body\":\"raw\ncontrol\"}"));
}

TEST(release_json_nested_names) {
  // "name" and "tag_name" inside other objects (author, nested asset fields, a top-level object
  // that isn't assets) are not assets[].name, nor the release's tag.
  auto r = gow3::setup::ParseRelease(R"({
    "author": {"name": "Flippage", "tag_name": "vX", "assets": [{"name": "wrong.zip"}]},
    "name": "Release 0.3.0",
    "tag_name": "v0.3.0",
    "assets": [
      {"url": "u", "uploader": {"name": "n", "list": [{"name": "deep"}]}, "name": "a.zip", "size": 12345,
       "extra": [1, -2.5e3, true, false, null, {}, []]},
      {"label": null, "name": "a.zip.sha256"}
    ],
    "reactions": {"assets": [{"name": "no"}]}
  })");
  CHECK(r && r->tag == "v0.3.0");
  CHECK(r && r->asset_names == std::vector<std::string>({"a.zip", "a.zip.sha256"}));
}

TEST(release_json_braces_in_strings) {
  auto r = gow3::setup::ParseRelease(
      R"({"body":"} ] { [ \" , : \"name\": \"x\" }","tag_name":"v0.4.0","assets":[{"name":"}{.zip"}]})");
  CHECK(r && r->body == R"(} ] { [ " , : "name": "x" })" && r->tag == "v0.4.0");
  CHECK(r && r->asset_names == std::vector<std::string>({"}{.zip"}));
}

TEST(release_json_malformed) {
  using gow3::setup::ParseRelease;
  CHECK(!ParseRelease(""));
  CHECK(!ParseRelease("{"));
  CHECK(!ParseRelease(R"({"tag_name":"v1"})" "x"));          // trailing garbage
  CHECK(!ParseRelease(R"({"tag_name":"v1",})"));             // trailing comma
  CHECK(!ParseRelease(R"({"tag_name" "v1"})"));              // missing colon
  CHECK(!ParseRelease(R"({"a":[1,2})"));                     // unclosed array
  CHECK(!ParseRelease(R"({"a":tru})"));                      // bad literal
  CHECK(!ParseRelease(R"({"a":01})"));                       // leading zero
  CHECK(!ParseRelease(R"({"a":1.})"));                       // no fraction digits
  CHECK(!ParseRelease(R"({"a":-})"));
  CHECK(!ParseRelease(R"({"tag_name":1})"));                 // wrong type for a read field
  CHECK(!ParseRelease(R"({"prerelease":"no"})"));
  CHECK(!ParseRelease(R"({"assets":{"name":"x"}})"));
  CHECK(!ParseRelease(R"({"assets":[{"name":5}]})"));
  CHECK(!ParseRelease(R"({"assets":["x"]})"));
  std::string deep(10000, '[');
  CHECK(!ParseRelease("{\"a\":" + deep));                     // no stack overflow on deep nesting
  // Valid with nothing we read, and white space everywhere.
  auto r = ParseRelease(" \r\n\t{ \"x\" : [ ] , \"y\" : { } , \"z\" : -0.5E+2 } \n");
  CHECK(r && r->tag.empty() && !r->prerelease && r->asset_names.empty());
}
