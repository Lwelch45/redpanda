#pragma once

#include "hashing/xx.h"
#include "utils/fragbuf.h"

#include <seastar/core/do_with.hh>
#include <seastar/core/iostream.hh>

#include <functional>

namespace rpc {

class source {
public:
    explicit source(input_stream<char>& s)
      : _source(std::ref(s)) {
    }
    source(source&&) noexcept = default;
    source& operator=(source&&) noexcept = default;

    future<temporary_buffer<char>> read_exactly(size_t i)  {
        return _source.get().read_exactly(i).then(
          [this](temporary_buffer<char> b) {
              if (b.size() > 0) {
                  _size += b.size();
                  _hash.update(b.get(), b.size());
              }
              return make_ready_future<temporary_buffer<char>>(std::move(b));
          });
    }
    future<fragbuf> read_fragbuf(size_t i)  {
        return _frag.read_exactly(_source.get(), i).then([this](fragbuf b) {
            auto istream = b.get_istream();
            istream.consume([this](bytes_view bv) {
                _hash.update(
                  reinterpret_cast<const char*>(bv.data()), bv.size());
            });
            _size += b.size_bytes();
            return std::move(b);
        });
    }
    template<typename Consumer>
    GCC6_CONCEPT(requires InputStreamConsumer<Consumer, char>)
    future<> consume(Consumer&& c) {
        return _source.get().consume([this, c = std::forward<Consumer>(c)](
                                       temporary_buffer<char> b) mutable {
            size_t sz = b.size();
            const char* begin = b.get();
            auto bb = b.share();
            return c(std::move(b)).finally([=, bb = std::move(bb)] {
                if (sz == bb.size()) {
                    return;
                }
                size_t delta = sz - bb.size();
                _hash.update(begin, delta);
                _size += delta;
            });
        });
    }
    size_t size_bytes() const {
        return _size;
    }
    uint64_t checksum() {
        return _hash.digest();
    }
    source(const source&) = delete;
    ~source() noexcept = default;

private:
    std::reference_wrapper<input_stream<char>> _source;
    incremental_xxhash64 _hash;
    fragbuf::reader _frag;
    size_t _size = 0;
};

} // namespace rpc