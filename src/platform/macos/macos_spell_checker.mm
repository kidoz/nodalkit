#include "macos_spell_checker.h"

#import <AppKit/AppKit.h>

namespace nk {

std::vector<SpellCheckRange> MacosSpellChecker::check(std::string_view text) {
    std::vector<SpellCheckRange> result;
    if (text.empty()) {
        return result;
    }

    @autoreleasepool {
        NSString* ns_text = [[NSString alloc] initWithBytes:text.data()
                                                     length:text.size()
                                                   encoding:NSUTF8StringEncoding];
        if (ns_text == nil) {
            return result;
        }

        NSSpellChecker* checker = [NSSpellChecker sharedSpellChecker];
        NSUInteger offset = 0;
        const NSUInteger total = [ns_text length];

        while (offset < total) {
            NSRange range = [checker checkSpellingOfString:ns_text startingAt:offset];
            if (range.location == NSNotFound || range.length == 0) {
                break;
            }

            NSString* misspelled = [ns_text substringWithRange:range];
            NSRange prefix_range = NSMakeRange(0, range.location);
            NSString* prefix = [ns_text substringWithRange:prefix_range];

            const std::size_t byte_start = [prefix lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
            const std::size_t byte_length =
                [misspelled lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

            result.push_back({byte_start, byte_length});
            offset = range.location + range.length;
        }
    }

    return result;
}

std::vector<std::string> MacosSpellChecker::suggestions(std::string_view misspelled_word) {
    std::vector<std::string> result;
    if (misspelled_word.empty()) {
        return result;
    }

    @autoreleasepool {
        NSString* ns_word = [[NSString alloc] initWithBytes:misspelled_word.data()
                                                     length:misspelled_word.size()
                                                   encoding:NSUTF8StringEncoding];
        if (ns_word == nil) {
            return result;
        }

        NSSpellChecker* checker = [NSSpellChecker sharedSpellChecker];
        NSArray<NSString*>* guesses = [checker guessesForWordRange:NSMakeRange(0, [ns_word length])
                                                          inString:ns_word
                                                          language:nil
                                            inSpellDocumentWithTag:0];

        result.reserve([guesses count]);
        for (NSString* guess in guesses) {
            const char* utf8 = [guess UTF8String];
            if (utf8 != nullptr) {
                result.emplace_back(utf8);
            }
        }
    }

    return result;
}

} // namespace nk
