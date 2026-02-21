#include "article_simplifier.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>

// ============================================================
//  TextAnalyzer
// ============================================================

// this is rough but the results seem reasonable enough
int TextAnalyzer::countSyllables(const std::string& rawWord) {
    std::string word = rawWord;
    std::transform(word.begin(), word.end(), word.begin(), ::tolower);

    int n = 0;
    bool lastWasVowel = false;
    std::string vowels = "aeiouy";

    for (char c : word) {
        bool v = vowels.find(c) != std::string::npos;
        if (v && !lastWasVowel) n++;
        lastWasVowel = v;
    }

    // silent e at the end
    if (word.size() > 2 && word.back() == 'e')
        n--;

    return std::max(1, n);
}

double TextAnalyzer::calcFlesch(double wps, double spw) {
    return 206.835 - (1.015 * wps) - (84.6 * spw);
}

TextAnalyzer::Metrics TextAnalyzer::analyze(const std::string& text) {
    Metrics m;

    std::vector<std::string> sentences;
    std::string cur;
    for (char c : text) {
        cur += c;
        if (c == '.' || c == '!' || c == '?') {
            sentences.push_back(cur);
            cur.clear();
        }
    }

    if (sentences.empty()) return m;

    int totalWords = 0;
    int totalSyllables = 0;

    for (auto& sent : sentences) {
        std::istringstream ss(sent);
        std::string tok;
        int wc = 0;
        while (ss >> tok) {
            tok.erase(std::remove_if(tok.begin(), tok.end(),
                [](char c) { return !std::isalpha(c); }), tok.end());
            if (tok.empty()) continue;
            wc++;
            totalSyllables += countSyllables(tok);
        }
        totalWords += wc;
    }

    m.avgWordsPerSentence = (double)totalWords / sentences.size();
    m.avgSyllablesPerWord = totalWords > 0 ? (double)totalSyllables / totalWords : 0.0;
    m.fleschScore = calcFlesch(m.avgWordsPerSentence, m.avgSyllablesPerWord);

    // these cutoffs are kind of made up, calibrate later
    double f = m.fleschScore;
    if      (f >= 80) m.cefrEstimate = 1;
    else if (f >= 65) m.cefrEstimate = 2;
    else if (f >= 50) m.cefrEstimate = 3;
    else if (f >= 40) m.cefrEstimate = 4;
    else if (f >= 25) m.cefrEstimate = 5;
    else              m.cefrEstimate = 6;

    return m;
}

// ============================================================
//  Vocabulary
// ============================================================

Vocabulary::Vocabulary(CEFRLevel lvl) : lvl_(lvl) {
    if (lvl == CEFRLevel::A1) loadA1();
    else                      loadA2();
}

void Vocabulary::loadA1() {
    // keeping this small for now
    wordMap_ = {
        {"utilize",      "use"},
        {"commence",     "start"},
        {"terminate",    "end"},
        {"residence",    "home"},
        {"purchase",     "buy"},
        {"inquire",      "ask"},
        {"observe",      "see"},
        {"obtain",       "get"},
        {"assistance",   "help"},
        {"demonstrate",  "show"},
        {"approximately","about"},
        {"sufficient",   "enough"},
        {"however",      "but"},
        {"therefore",    "so"},
        {"additionally", "also"},
        {"attempt",      "try"},
        {"require",      "need"},
    };
}

void Vocabulary::loadA2() {
    loadA1();
    // A2 can handle a bit more
    wordMap_["facilitate"] = "help";
    wordMap_["construct"]  = "build";
    wordMap_["complete"]   = "finish";
    wordMap_["numerous"]   = "many";
    wordMap_["previously"] = "before";
    // TODO: this needs to be way bigger, maybe pull from Oxford 3000
}

bool Vocabulary::isSimple(const std::string& word) const {
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return wordMap_.find(lower) == wordMap_.end();
}

std::string Vocabulary::getSimplerWord(const std::string& word) const {
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto it = wordMap_.find(lower);
    if (it != wordMap_.end()) return it->second;
    return word;
}

// ============================================================
//  SentenceRewriter
// ============================================================

SentenceRewriter::SentenceRewriter(CEFRLevel lvl, const Vocabulary& v)
    : lvl_(lvl), vocab_(v) {}

std::string SentenceRewriter::swapWords(const std::string& s) const {
    std::istringstream ss(s);
    std::string out, tok;
    while (ss >> tok) {
        std::string punct;
        while (!tok.empty() && std::ispunct((unsigned char)tok.back())) {
            punct = tok.back() + punct;
            tok.pop_back();
        }
        out += vocab_.getSimplerWord(tok) + punct + " ";
    }
    if (!out.empty()) out.pop_back();
    return out;
}

std::vector<std::string> SentenceRewriter::trySplit(const std::string& s) const {
    // split anything over ~10 words (A1) or ~15 words (A2)
    // the splitting logic here is pretty dumb right now
    int limit = (lvl_ == CEFRLevel::A1) ? 10 : 15;

    std::istringstream ss(s);
    std::string tok;
    std::vector<std::string> words;
    while (ss >> tok) words.push_back(tok);

    if ((int)words.size() <= limit)
        return { s };

    std::vector<std::string> chunks;
    std::string chunk;
    int count = 0;
    for (auto& w : words) {
        chunk += w + " ";
        count++;
        std::string lw = w;
        std::transform(lw.begin(), lw.end(), lw.begin(), ::tolower);
        // split on conjunctions when we're past the halfway point
        if ((lw == "and" || lw == "but" || lw == "because") && count >= limit / 2) {
            chunks.push_back(chunk);
            chunk.clear();
            count = 0;
        }
    }
    if (!chunk.empty()) chunks.push_back(chunk);
    return chunks;
}

std::string SentenceRewriter::stripParens(const std::string& s) const {
    // only strip for A1, A2 readers can probably handle it
    if (lvl_ != CEFRLevel::A1) return s;
    return std::regex_replace(s, std::regex(R"(\([^)]*\))"), "");
}

std::string SentenceRewriter::fixPassive(const std::string& s) const {
    // TODO: this is a whole thing, skipping for now
    // "the ball was kicked by john" -> "john kicked the ball"
    // need to actually think about how to detect this reliably
    return s;
}

std::vector<std::string> SentenceRewriter::rewrite(const std::string& sentence) const {
    std::string s = stripParens(sentence);
    s = swapWords(s);
    s = fixPassive(s);  // no-op right now
    return trySplit(s);
}

// ============================================================
//  Simplifier
// ============================================================

Simplifier::Simplifier(CEFRLevel lvl)
    : lvl_(lvl), vocab_(lvl), rewriter_(lvl, vocab_), progressFn_(nullptr) {}

void Simplifier::setProgress(std::function<void(int, int)> fn) {
    progressFn_ = fn;
}

std::vector<std::string> Simplifier::splitSentences(const std::string& text) const {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        cur += c;
        if ((c == '.' || c == '!' || c == '?') && !cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string Simplifier::rejoin(const std::vector<std::string>& parts) const {
    std::string out;
    for (auto& p : parts) {
        std::string s = p;

        auto start = s.find_first_not_of(" \t\n");
        if (start != std::string::npos) s = s.substr(start);
        if (s.empty()) continue;

        s[0] = std::toupper(s[0]);

        if (s.back() != '.' && s.back() != '!' && s.back() != '?')
            s += '.';

        out += s + " ";
    }
    return out;
}

SimplifiedArticle Simplifier::run(const std::string& text) const {
    auto sentences = splitSentences(text);
    int total = (int)sentences.size();

    std::vector<std::string> result;
    for (int i = 0; i < total; i++) {
        auto parts = rewriter_.rewrite(sentences[i]);
        for (auto& p : parts) result.push_back(p);

        if (progressFn_) progressFn_(i + 1, total);
    }

    SimplifiedArticle out;
    out.original   = text;
    out.simplified = rejoin(result);
    out.level      = lvl_;
    return out;
}

// ============================================================
//  CLI
// ============================================================

void CLI::banner() const {
    std::cout << "\n--- article simplifier (wip) ---\n";
    std::cout << "a1 = beginner / a2 = elementary\n\n";
}

void CLI::showMetrics(const TextAnalyzer::Metrics& m) const {
    const char* labels[] = { "?", "A1", "A2", "B1", "B2", "C1", "C2" };
    int idx = (m.cefrEstimate >= 1 && m.cefrEstimate <= 6) ? m.cefrEstimate : 0;
    std::cout << "  flesch score:       " << (int)std::round(m.fleschScore) << "\n";
    std::cout << "  avg words/sentence: " << (int)std::round(m.avgWordsPerSentence) << "\n";
    std::cout << "  estimated level:    " << labels[idx] << "\n\n";
}

CEFRLevel CLI::pickLevel() const {
    std::cout << "output level:\n";
    std::cout << "  1 = A1 (beginner)\n";
    std::cout << "  2 = A2 (elementary)\n";
    std::cout << "> ";
    int choice;
    std::cin >> choice;
    std::cin.ignore();
    return (choice == 1) ? CEFRLevel::A1 : CEFRLevel::A2;
}

std::string CLI::getArticle() const {
    std::cout << "paste article, then type END on a new line:\n\n";
    std::string article, line;
    while (std::getline(std::cin, line)) {
        if (line == "END") break;
        article += line + "\n";
    }
    return article;
}

void CLI::printResult(const SimplifiedArticle& r) const {
    const char* lvl = (r.level == CEFRLevel::A1) ? "A1" : "A2";
    std::cout << "\n[original]\n" << r.original;
    std::cout << "\n[simplified - " << lvl << "]\n" << r.simplified << "\n";
}

void CLI::run() {
    banner();

    while (true) {
        std::string text = getArticle();
        if (text.empty()) break;

        std::cout << "\noriginal metrics:\n";
        showMetrics(TextAnalyzer::analyze(text));

        CEFRLevel lvl = pickLevel();

        Simplifier s(lvl);
        s.setProgress([](int done, int total) {
            std::cout << "\r  processing... " << done << "/" << total << std::flush;
        });

        std::cout << "\n";
        auto result = s.run(text);

        std::cout << "\nsimplified metrics:\n";
        showMetrics(TextAnalyzer::analyze(result.simplified));

        printResult(result);

        std::cout << "another? [y/n]: ";
        char again;
        std::cin >> again;
        std::cin.ignore();
        if (again != 'y') break;
    }
}

int main() {
    CLI cli;
    cli.run();
    return 0;
}
