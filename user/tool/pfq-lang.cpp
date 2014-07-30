/***************************************************************
 *
 * (C) 2014 - Nicola Bonelli <nicola.bonelli@cnit.it>
 *
 ****************************************************************/

#include <iostream>
#include <fstream>
#include <sstream>

#include <thread>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <iterator>
#include <atomic>
#include <cmath>
#include <tuple>

#include <pfq.hpp>
#include <pfq-lang/lang.hpp>
#include <pfq-lang/default.hpp>

#include <affinity.hpp>
#include <binding.hpp>
#include <vt100.hpp>

using namespace pfq;
using namespace pfq::lang;


///////////////////////////////////////////

    auto computation = unit;

///////////////////////////////////////////


namespace opt
{
    int sleep_microseconds;
    std::string function;

    size_t caplen = 64;
    size_t slots  = 131072;
    bool flow     = false;
}


namespace thread
{
    struct context
    {
        context(int id, const binding &b)
        : m_id(id)
        , m_bind(b)
        , m_stop(std::unique_ptr<std::atomic_bool>(new std::atomic_bool(false)))
        , m_pfq(group_policy::undefined, opt::caplen, opt::slots)
        , m_read()
        , m_batch()
        {
            if (m_bind.gid == -1)
                m_bind.gid = id;

            m_pfq.join_group(m_bind.gid, group_policy::shared);

            for(auto &d : m_bind.dev)
            {
                if (m_bind.queue.empty())
                {
                    m_pfq.bind_group(m_bind.gid, d.c_str(), -1);
                    std::cout << "+ bind to " << d << "@" << -1 << std::endl;
                }
                else
                    for(auto q : m_bind.queue)
                    {
                        std::cout << "+ bind to " << d << "@" << q << std::endl;
                        m_pfq.bind_group(m_bind.gid, d.c_str(), q);
                    }
            }

            if (!opt::function.empty() && (m_id == 0))
            {
                std::cout << "fun: " << opt::function << std::endl;
                m_pfq.set_group_computation(m_bind.gid, opt::function);
            }
            else
            {
                std::cout << "fun: " << pretty(computation) << std::endl;
                m_pfq.set_group_computation(m_bind.gid, computation);
            }

            m_pfq.timestamp_enable(false);

            m_pfq.enable();
        }

        context(const context &) = delete;
        context& operator=(const context &) = delete;

        context(context &&) = default;
        context& operator=(context &&) = default;

        void operator()()
        {
            for(;;)
            {
                auto many = m_pfq.read(opt::sleep_microseconds);

                m_read += many.size();

                m_batch = std::max(m_batch, many.size());

                if (m_stop->load(std::memory_order_relaxed))
                    return;
            }
        }

        void stop()
        {
            m_stop->store(true, std::memory_order_release);
        }

        pfq_stats
        stats() const
        {
            return m_pfq.stats();
        }

        unsigned long long
        read() const
        {
            return m_read;
        }

        size_t
        batch() const
        {
            return m_batch;
        }

    private:
        int m_id;
        binding m_bind;

        std::unique_ptr<std::atomic_bool> m_stop;

        pfq::socket m_pfq;

        unsigned long long m_read;
        size_t m_batch;

    };

}


void usage(std::string name)
{
    throw std::runtime_error
    (
        "usage: " + std::move(name) + " [OPTIONS]\n\n"
        " -h --help                     Display this help\n"
        " -c --caplen=INT               Set caplen\n"
        " -s --slot=INT                 Set slots\n"
        " -f --function=FUNCTION\n"
        " -t --thread=BINDING\n\n"
        "      BINDING = eth0:...:ethx[.core[.gid[.queue.queue...]]]\n"
        "      FUNCTION = fun[ >-> fun >-> fun]"
    );
}


int
main(int argc, char *argv[])
try
{
    if (argc < 2)
        usage(argv[0]);

    std::vector<std::thread> vt;
    std::vector<thread::context> ctx;

    std::vector<binding> thread_binding;

    for(int i = 1; i < argc; ++i)
    {
        if ( strcmp(argv[i], "-f") == 0 ||
             strcmp(argv[i], "--fun") == 0) {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("group function missing");
            }
            opt::function.assign(argv[i]);
            continue;
        }

        if ( strcmp(argv[i], "-c") == 0 ||
             strcmp(argv[i], "--caplen") == 0) {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("caplen missing");
            }

            opt::caplen = std::atoi(argv[i]);
            continue;
        }

        if ( strcmp(argv[i], "-s") == 0 ||
             strcmp(argv[i], "--slots") == 0) {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("slots missing");
            }

            opt::slots = std::atoi(argv[i]);
            continue;
        }

        if ( strcmp(argv[i], "-w") == 0 ||
             strcmp(argv[i], "--flow") == 0)
        {
            opt::flow = true;
            continue;
        }

        if ( strcmp(argv[i], "-t") == 0 ||
             strcmp(argv[i], "--thread") == 0) {
            i++;
            if (i == argc)
            {
                throw std::runtime_error("descriptor missing");
            }

            thread_binding.push_back(make_binding(argv[i]));
            continue;
        }

        if ( strcmp(argv[i], "-h") == 0 ||
             strcmp(argv[i], "-?") == 0 ||
             strcmp(argv[i], "--help") == 0
             )
            usage(argv[0]);

        throw std::runtime_error(std::string(argv[i]) + " unknown option!");
    }

    std::cout << "caplen: " << opt::caplen << std::endl;
    std::cout << "slots : " << opt::slots << std::endl;

    if (opt::slots < 1024)
    {
        std::cout << "too few slots may affect the performance!" << std::endl;
        _Exit(0);
    }

    // create thread context:
    //
    for(unsigned int i = 0; i < thread_binding.size(); ++i)
    {
        ctx.push_back(thread::context(i, thread_binding[i]));
    }

    opt::sleep_microseconds = 50000 * ctx.size();
    std::cout << "poll timeout " << opt::sleep_microseconds << " usec" << std::endl;

    // create threads:
    //

    int i = 0;
    std::for_each(thread_binding.begin(), thread_binding.end(), [&](binding &b) {

                  std::thread t(std::ref(ctx[i++]));

                  std::cout << "thread: " << show_binding(b) << std::endl;

                  if (b.core != -1)
                  extra::set_affinity(t, b.core);

                  vt.push_back(std::move(t));
                  });

    unsigned long long sum, old = 0;
    pfq_stats sum_stats, old_stats = {0,0,0,0,0};

    std::cout << "----------- capture started ------------\n";

    auto begin = std::chrono::system_clock::now();

    for(int y=0;; y++)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        sum = 0;
        sum_stats = {0,0,0,0,0};

        std::for_each(ctx.begin(), ctx.end(), [&](const thread::context &c) {
                      sum += c.read();
                      sum_stats += c.stats();
                      });

        std::cout << "recv: ";
        std::for_each(ctx.begin(), ctx.end(), [&](const thread::context &c) {
                      std::cout << c.stats().recv << ' ';
                      });
        std::cout << " -> " << sum_stats.recv << std::endl;

        std::cout << "lost: ";
        std::for_each(ctx.begin(), ctx.end(), [&](const thread::context &c) {
                      std::cout << c.stats().lost << ' ';
                      });
        std::cout << " -> " << sum_stats.lost << std::endl;

        std::cout << "drop: ";
        std::for_each(ctx.begin(), ctx.end(), [&](const thread::context &c) {
                      std::cout << c.stats().drop << ' ';
                      });
        std::cout << " -> " << sum_stats.drop << std::endl;

        std::cout << "max_batch: ";
        std::for_each(ctx.begin(), ctx.end(), [&](const thread::context &c) {
                      std::cout << c.batch() << ' ';
                      });
        std::cout << std::endl;

        auto end = std::chrono::system_clock::now();

        std::cout << "capture: " << vt100::BOLD <<
        (static_cast<uint64_t>(sum-old)*1000000)/std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count()
        << vt100::RESET << " pkt/sec";


        std::cout << std::endl;

        old = sum, begin = end;
        old_stats = sum_stats;
    }

    std::for_each(ctx.begin(), ctx.end(), std::mem_fn(&thread::context::stop));
    std::for_each(vt.begin(), vt.end(), std::mem_fn(&std::thread::join));

    return 0;
}
catch(std::exception &e)
{
    std::cerr << e.what() << std::endl;
}