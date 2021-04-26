//---------------------------------------------------------------------------//
//        ____  _____________  __    __  __ _           _____ ___   _        //
//       / __ \/ ____/ ___/\ \/ /   |  \/  (_)__ _ _ __|_   _/ __| /_\       //
//      / / / / __/  \__ \  \  /    | |\/| | / _| '_/ _ \| || (__ / _ \      //
//     / /_/ / /___ ___/ /  / /     |_|  |_|_\__|_| \___/|_| \___/_/ \_\     //
//    /_____/_____//____/  /_/      T  E  C  H  N  O  L  O  G  Y   L A B     //
//                                                                           //
//---------------------------------------------------------------------------//

// Copyright (c) 2021 Deutsches Elektronen-Synchrotron DESY

#include <csignal>
#include <iostream>
#include <stdexcept>
#include <future>

#include <boost/log/core/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/keywords/severity.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include "udmaio/FpgaMemBuffer.hpp"
#include "udmaio/UDmaBuf.hpp"
#include "udmaio/UioAxiDmaIf.hpp"
#include "udmaio/UioIf.hpp"
#include "udmaio/UioIfFactory.hpp"
#include "udmaio/UioMemSgdma.hpp"

#include "DataHandlerPrint.hpp"
#include "DmaMode.hpp"
#include "UioGpioStatus.hpp"
#include "UioTrafficGen.hpp"
#include "ZupExampleProjectConsts.hpp"

namespace blt = boost::log::trivial;
namespace bpo = boost::program_options;

using namespace udmaio;
using namespace std::chrono_literals;

volatile bool g_stop_loop = false;

void signal_handler([[maybe_unused]] int signal) { g_stop_loop = true; }

int main(int argc, char *argv[]) {
    bpo::options_description desc("AXI DMA demo");
    bool debug, trace;
    uint16_t pkt_pause;
    uint16_t nr_pkts;
    uint32_t pkt_len;
    DmaMode mode;
    std::string dev_path;

    // clang-format off
    desc.add_options()
    ("help,h", "this help")
    ("mode", bpo::value<DmaMode>(&mode)->multitoken()->required(), "select operation mode - see docs for details")
    ("debug", bpo::bool_switch(&debug), "enable verbose output (debug level)")
    ("trace", bpo::bool_switch(&trace), "enable even more verbose output (trace level)")
    ("pkt_pause", bpo::value<uint16_t>(&pkt_pause)->default_value(10), "pause between pkts - see AXI TG user's manual")
    ("nr_pkts", bpo::value<uint16_t>(&nr_pkts)->default_value(1), "number of packets to generate - see AXI TG user's manual")
    ("pkt_len", bpo::value<uint32_t>(&pkt_len)->default_value(1024), "packet length - see AXI TG user's manual")
    ("dev_path", bpo::value<std::string>(&dev_path), "Path to xdma device nodes")
    ;
    // clang-format on

    bpo::variables_map vm;
    bpo::store(bpo::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    bpo::notify(vm);

    if (mode == DmaMode::XDMA && dev_path.empty()) {
        std::cerr << "XDMA mode needs path to device (--dev-path)" << std::endl;
        return 0;
    }

    if (trace) {
        boost::log::core::get()->set_filter(blt::severity >= blt::trace);
    } else if (debug) {
        boost::log::core::get()->set_filter(blt::severity >= blt::debug);
    } else {
        boost::log::core::get()->set_filter(blt::severity >= blt::info);
    }

    std::signal(SIGINT, signal_handler);

    auto gpio_status = (mode == DmaMode::UIO)
            ? UioIfFactory::create_from_uio<UioGpioStatus>("axi_gpio_status")
            : UioIfFactory::create_from_xdma<UioGpioStatus>(
                dev_path,
                zup_example_prj::axi_gpio_status,
                zup_example_prj::pcie_axi4l_offset
            );
    bool is_ddr4_init = gpio_status->is_ddr4_init_calib_complete();
    BOOST_LOG_TRIVIAL(debug) << "DDR4 init = " << is_ddr4_init;
    if (!is_ddr4_init) {
        throw std::runtime_error("DDR4 init calib is not complete");
    }

    auto axi_dma = (mode == DmaMode::UIO)
                       ? UioIfFactory::create_from_uio<UioAxiDmaIf>("hier_daq_arm_axi_dma_0")
                       : UioIfFactory::create_from_xdma<UioAxiDmaIf>(
                           dev_path,
                           zup_example_prj::axi_dma_0,
                           zup_example_prj::pcie_axi4l_offset,
                           "events0"
                        );

    auto mem_sgdma =
        (mode == DmaMode::UIO)
            ? UioIfFactory::create_from_uio<UioMemSgdma>("hier_daq_arm_axi_bram_ctrl_0")
            : UioIfFactory::create_from_xdma<UioMemSgdma>(
                dev_path,
                zup_example_prj::bram_ctrl_0,
                zup_example_prj::pcie_axi4l_offset
            );
    auto traffic_gen =
        (mode == DmaMode::UIO)
            ? UioIfFactory::create_from_uio<UioTrafficGen>("hier_daq_arm_axi_traffic_gen_0")
            : UioIfFactory::create_from_xdma<UioTrafficGen>(
                dev_path,
                zup_example_prj::axi_traffic_gen_0,
                zup_example_prj::pcie_axi4l_offset
            );

    auto udmabuf =
        (mode == DmaMode::UIO)
            ? static_cast<std::unique_ptr<DmaBufferAbstract>>(std::make_unique<UDmaBuf>())
            : static_cast<std::unique_ptr<DmaBufferAbstract>>(std::make_unique<FpgaMemBuffer>(
                dev_path,
                zup_example_prj::fpga_mem_phys_addr
            ));

    DataHandlerPrint data_handler{
        *axi_dma,
        *mem_sgdma,
        *udmabuf,
        nr_pkts * pkt_len * zup_example_prj::lfsr_bytes_per_beat
    };
    auto fut = std::async(std::launch::async, std::ref(data_handler));

    std::vector<uintptr_t> dst_buf_addrs;
    for (int i = 0; i < 32; i++) {
        dst_buf_addrs.push_back(udmabuf->get_phys_addr() + i * mem_sgdma->BUF_LEN);
    };

    mem_sgdma->write_cyc_mode(dst_buf_addrs);

    uintptr_t first_desc = mem_sgdma->get_first_desc_addr();
    axi_dma->start(first_desc);
    traffic_gen->start(nr_pkts, pkt_len, pkt_pause);

    // Wait until data_handler has finished or user hit Ctrl-C
    while (fut.wait_for(10ms) != std::future_status::ready) {
        if (g_stop_loop) {
            data_handler.stop();
            fut.wait();
            break;
        }
    }

    traffic_gen->stop();

    auto [counter_ok, counter_total] = fut.get();
    std::cout << "Counters: OK = " << counter_ok << ", total = " << counter_total << "\n";

    return !(counter_ok == counter_total);
}
