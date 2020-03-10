#ifndef PARSE_HPP
#define PARSE_HPP

#include <cstdio>
#include <cinttypes>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <cassert>
#include <zlib.h>
#include <sdsl/int_vector.hpp>
#include "kseq.h"
extern "C" {
KSEQ_INIT(gzFile, gzread);
#include "utils.h"
#include "gsa/gsacak.h"
}

namespace pfbwtf {

struct ParserParams {
    std::string fname;
    size_t w = 10;
    size_t p = 100;
    bool get_sai = false;
    bool get_da = false;
    bool verbose = false;
    // if bothm trim_non_acgt and non_acgt_to_a are set, trim_non_acgt takes precedence
    bool trim_non_acgt = false;
    bool non_acgt_to_a = false;
    bool print_docs = false;
};

template<typename T>
struct Freq {
    Freq(T x) : n(x) {}
    T n = 0;
    T r = 0;
};

struct ntab_entry {
    size_t pos = 0;
    size_t l = 0;
    void clear() {
        pos = 0; l = 0;
    }
};

template <typename Hasher>
struct Parser {

    public:

    using UIntType = uint_t;

    Parser(ParserParams p) : params(p) {
        check_w(p.w);
    }

    // if get_sai is true, then an UIntType is stored for every phrase
    // encountered (ie., the phrases' position within the text).
    // We might consider writing the UIntType to file instead in the case that
    // the number of phrases is huge.
    size_t parse_fasta() {
        size_t n = 0;
        if (params.get_sai) {
            sai.clear();
            // TODO: figure out how to check if file is gzipped and prevent this
            if (params.fname != "-") {
                sai.reserve(get_file_size(params.fname.data())+1);
            }
        }
        if (params.get_da) {
            doc_starts.clear();
            doc_names.clear();
        }
        gzFile fp;
        if (params.fname == "-") {
            fp = gzdopen(fileno(stdin), "r");
        } else {
            fp = gzopen(params.fname.data(), "r");
        }
        if (fp == NULL) die("failed to open file!\n");
        std::FILE* docs_fp = NULL;
        if (params.print_docs) {
            docs_fp = open_aux_file(params.fname.data(), "docs", "w");
        }
        kseq_t* seq = kseq_init(fp);
        int l;
        uint64_t nseqs(0);
#if !M64
        uint64_t total_l(0);
#endif
        UIntType pos(0);
        char c('A'), pc('A');
        ntab_entry ne;
        std::string phrase;
        phrase.append(1, Dollar);
        Hasher hf(params.w);
        while (( l = kseq_read(seq) ) >= 0) {
            if (params.get_da) {
                doc_starts.push_back(pos);
                doc_names.push_back(seq->name.s);
            }
            if (params.print_docs) {
                fprintf(docs_fp, "%s\t%lu\n", seq->name.s, static_cast<uint64_t>(pos));
            }
#if !M64
            if (total_l + l > 0xFFFFFFFF) {
                fprintf(stderr, "size: %lu\n", total_l + l);
                die("input too long, please use 64-bit version");
            }
            total_l += l;
#endif
            for (size_t i = 0; i < seq->seq.l; ++i) {
                c = std::toupper(seq->seq.s[i]);
                if (params.trim_non_acgt) {
                    char x = seq_nt4_table[static_cast<size_t>(pc)];
                    char y = seq_nt4_table[static_cast<size_t>(c)];
                    if (y > 3) { // skip if nonACGT
                        if (x < 4) { // new N run
                            ne.l = 1;
                            ne.pos = pos-1;
                        } else {
                            ne.l += 1;
                        }
                        pc = c;
                        continue; // make sure that rest of loop is skipped
                    }
                    if (y < 4 && x > 3) { // record if [^ACGT] run ended
                        ntab.push_back(ne);
                        ne.clear();
                    }
                } else if (params.non_acgt_to_a && seq_nt4_table[static_cast<size_t>(c)] > 3) {
                    c = 'A';
                }
                phrase.append(1, c);
                hf.update(c);
                if (hf.hashvalue() % params.p == 0) {
                    process_phrase(phrase);
                    if (params.get_sai) {
                        sai.push_back(pos+1);
                    }
                    phrase.erase(0, phrase.size()-params.w);
                }
                ++n;
                ++pos;
                pc = c;
            }
            // record last [^ACGT] run if applicable
            if (params.trim_non_acgt && seq_nt4_table[static_cast<size_t>(c)] > 3) {
                ntab.push_back(ne);
            }
            ++nseqs;
        }
        phrase.append(params.w, Dollar);
        process_phrase(phrase);
        if (params.get_sai) {
            sai.push_back(pos + params.w);
        }
        if (params.print_docs) fclose(docs_fp);
        kseq_destroy(seq);
        gzclose(fp);
        return n;
    }

    // assigns lexicographic rankings to items in dictionary
    // and creates occ array
    template<typename DictFn>
    void update_dict(DictFn dict_fn) {
        std::vector<const char*> dict_phrases;
        dict_phrases.reserve(freqs.size());
        for (auto it = freqs.begin(); it != freqs.end(); ++it) {
            dict_phrases.push_back(it->first.data());
        }
        std::sort(dict_phrases.begin(), dict_phrases.end(),
                [](const char* l, const char* r) { return strcmp(l, r) <= 0; });
        occs.clear();
        occs.reserve(dict_phrases.size());
        size_t rank = 1;
        for (auto x: dict_phrases) {
            // access dictionary and write occ to occ file
            auto& wf = freqs.at(x);
            wf.r = rank++;
            occs.push_back(wf.n);
            dict_fn(x, wf.n);
        }
    }

    void generate_parse_ranks() {
        if (!freqs.size()) {
            fprintf(stderr, "dictionary not created yet! run update_dict");
            exit(1);
        }
        clear_parse_ranks();
        parse_ranks.reserve(parse.size());
        occs.reserve(parse.size());
        for (auto phrase: parse) {
            auto wf = freqs.at(std::string(phrase));
            parse_ranks.push_back(wf.r);
        }
        parse_size = parse_ranks.size();
    }

    void clear_dict() {
        freqs.clear();
    }

    void clear_parse() {
        parse.clear();
    }

    void clear_parse_ranks() {
        parse_ranks.clear();
    }

    void clear_occ() {
        occs.clear();
    }

    void clear_last() {
        last.clear();
    }

    void clear() {
        clear_dict();
        clear_parse();
        clear_parse_ranks();
        clear_last();
    }

    void check_w(size_t x) {
        if (x > 32){
            fprintf(stderr, "window size w must be < 32!\n");
            exit(1);
        }
        clear();
    }

    const std::vector<const char*>& get_parse() const {
        return parse;
    }

    const std::vector<int_text>& get_parse_ranks() const {
        if (!parse_ranks.size()) {
            die("parse ranks have not been generated!");
        }
        return parse_ranks;
    }

    // generates bwlast and ilist (and bwsai)
    template<typename OutFn>
    void bwt_of_parse(OutFn out_fn) {
        // these will get passed to out_fn at end
        std::vector<char> bwlast;
        std::vector<UIntType> ilist;
        std::vector<UIntType> bwsai;

        size_t n; // size of parse_ranks, minus the last EOS character
        // TODO: support large parse sizes
        if (!parse_ranks.size()) generate_parse_ranks();
        if (parse_ranks.size() == 1) {
            die("error: only one dict word total. Re-run with a smaller p modulus");
        }
#if !M64
        // if in 32 bit mode, the number of words is at most 2^31-2
        if(parse_ranks.size() > 0x7FFFFFFE) {
            fprintf(stderr, "parse ranks size: %lu\n", parse_ranks.size());
            die("Input containing more than 2^31-2 phrases! Please use 64 bit version");
        }
#else
        // if in 64 bit mode, the number of words is at most 2^32-2 (for now)
        if(parse_ranks.size() > 0xFFFFFFFEu) {
            fprintf(stderr, "parse ranks size: %lu\n", parse_ranks.size());
            die("Input containing more than 2^32-2 phrases! This is currently a hard limit");
        }
#endif
        // add EOS if it's not already there
        if (parse_ranks[parse_ranks.size()-1]) {
            n = parse_ranks.size();
            parse_ranks.push_back(0);
        } else n = parse_ranks.size() - 1;
        // TODO: calculate k
        size_t k = 0;
        for (size_t i = 0; i < n; ++i) {
            k = parse_ranks[i] > k ? parse_ranks[i] : k;
        }
        if (params.get_sai) {
            bwsai.clear();
            bwsai.reserve(n);
        }
        // compute S.A.
        // we assign instead of reserve in order to be able to use .size()
        assert(sizeof(UIntType) >= sizeof(uint_t));
        std::vector<UIntType> SA(n+1, 0);
        // fprintf(stderr, "Computing S.A. of size %ld over an alphabet of size %ld\n",n+1,k+1);
        int depth = sacak_int(parse_ranks.data(), SA.data(), n+1, k+1);
        if (depth < 0) die("Error computing SA");
        // if(depth>=0) fprintf(stderr, "S.A. computed with depth: %d\n", depth);
        // else die("Error computing the S.A.");
        // transform S.A. to BWT in place
        assert(SA[0] == n);
        SA[0] = parse_ranks[n-1];
        bwlast.clear();
        bwlast.reserve(n+1);
        bwlast.push_back(last[n-2]);
        if (params.get_sai) bwsai.push_back(sai[n-1]);
        for (size_t i = 1; i < n+1; ++i) {
            if (!SA[i]) {
                SA[i] = 0;
                bwlast.push_back(0);
                // TODO: write to bwsai
                if (params.get_sai) bwsai.push_back(0);
            } else {
                if (SA[i] == 1) {
                    bwlast.push_back(last[n-1]);
                } else {
                    bwlast.push_back(last[SA[i]-2]);
                }
                if (params.get_sai) bwsai.push_back(sai[SA[i]-1]);
                SA[i] = parse_ranks[SA[i] - 1];
            }
        }
        std::vector<UIntType> F(occs.size()+1, 0);
        F[1] = 1;
        for (size_t i = 2; i < occs.size() + 1; ++i) {
            F[i] = F[i-1] + occs[i-2];
        }
        assert(F[occs.size()] + occs[occs.size()-1] == n+1);
        // TODO: do we want to store ilist as a bitvector directly?
        ilist.resize(n+1, 0);
        for (size_t i = 0; i < n + 1; ++i) {
            ilist[F[SA[i]]++] = i;
        }
        // ilist_processor(ilist);
        assert(ilist[0]==1);
        assert(SA[ilist[0]] == 0);
        out_fn(bwlast, ilist, bwsai);
    }

    size_t get_parse_size() const { return parse_size; }

    const std::vector<UIntType>& get_doc_starts() const {
        return doc_starts;
    }

    const std::vector<std::string>& get_doc_names() const {
        return doc_names;
    }

    const std::vector<ntab_entry>&  get_ntab() const {
        return ntab;
    }

    private:

    using FreqMap = std::map<std::string, Freq<UIntType>>;

    void inline process_phrase(const std::string& phrase) {
        auto ret = freqs.insert({phrase, Freq<UIntType>(1)});
        if (!ret.second) ret.first->second.n += 1;
        parse.push_back(ret.first->first.data());
        last.push_back(phrase[phrase.size()-params.w-1]);
    }

    FreqMap freqs;
    std::vector<const char*> parse;
    std::vector<UIntType> occs;
    std::vector<int_text> parse_ranks;
    std::vector<char> last;
    std::vector<UIntType> sai;
    std::vector<UIntType> doc_starts;
    std::vector<std::string> doc_names;
    std::vector<ntab_entry> ntab;
    size_t parse_size;
    ParserParams params;
};
}; // namespace end

#endif
