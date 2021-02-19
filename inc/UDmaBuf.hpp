//---------------------------------------------------------------------------//
//        ____  _____________  __    __  __ _           _____ ___   _        //
//       / __ \/ ____/ ___/\ \/ /   |  \/  (_)__ _ _ __|_   _/ __| /_\       //
//      / / / / __/  \__ \  \  /    | |\/| | / _| '_/ _ \| || (__ / _ \      //
//     / /_/ / /___ ___/ /  / /     |_|  |_|_\__|_| \___/|_| \___/_/ \_\     //
//    /_____/_____//____/  /_/      T  E  C  H  N  O  L  O  G  Y   L A B     //
//                                                                           //
//---------------------------------------------------------------------------//

// Copyright (c) 2021 Deutsches Elektronen-Synchrotron DESY

#pragma once

#include <boost/log/core/core.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/trivial.hpp>

#include "DmaBufferAbstract.hpp"

namespace blt = boost::log::trivial;

class UDmaBuf : public DmaBufferAbstract {
    int _fd;
    void *_mem;
    uint64_t _mem_size;
    uint64_t _phys_addr;
    boost::log::sources::severity_logger<blt::severity_level> _slg;

    uint64_t _get_size(int buf_idx);
    uint64_t _get_phys_addr(int buf_idx);

  public:
    explicit UDmaBuf(int buf_idx = 0);

    ~UDmaBuf();

    uint64_t get_phys_addr();

    void copy_from_buf(uint64_t buf_addr, uint32_t len, std::vector<uint8_t> &out);
};
