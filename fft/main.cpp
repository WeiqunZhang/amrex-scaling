#include <AMReX.H>
#include <AMReX_FFT.H>
#include <AMReX_MultiFab.H>
#include <AMReX_ParmParse.H>

#if defined(USE_HEFFTE)
#include <heffte.h>
#endif

using namespace amrex;

namespace {
    constexpr int ntests = 3;
}

double test_amrex_pencil (Box const& domain, MultiFab& mf, cMultiFab& cmf)
{
    FFT::R2C<Real,FFT::Direction::both,FFT::DomainStrategy::pencil> r2c(domain);
    r2c.forward(mf, cmf);
    r2c.backward(cmf, mf);

    Gpu::synchronize();    
    double t0 = amrex::second();

    for (int itest = 0; itest < ntests; ++itest) {
        r2c.forward(mf, cmf);
        r2c.backward(cmf, mf);
    }

    Gpu::synchronize();
    double t1 = amrex::second();

    return (t1-t0) / double(ntests);
}

double test_amrex_plane (Box const& domain, MultiFab& mf, cMultiFab& cmf)
{
    FFT::R2C<Real,FFT::Direction::both,FFT::DomainStrategy::plane> r2c(domain);
    r2c.forward(mf, cmf);
    r2c.backward(cmf, mf);

    Gpu::synchronize();    
    double t0 = amrex::second();

    for (int itest = 0; itest < ntests; ++itest) {
        r2c.forward(mf, cmf);
        r2c.backward(cmf, mf);
    }

    Gpu::synchronize();
    double t1 = amrex::second();

    return (t1-t0) / double(ntests);
}

#ifdef USE_HEFFTE
double test_heffte (Box const& /*domain*/, MultiFab& mf, cMultiFab& cmf)
{
    auto& fab = mf[ParallelDescriptor::MyProc()];
    auto& cfab = cmf[ParallelDescriptor::MyProc()];

    auto const& local_box = fab.box();
    auto const& c_local_box = cfab.box();

#ifdef AMREX_USE_CUDA
    heffte::fft3d_r2c<heffte::backend::cufft> fft
#elif AMREX_USE_HIP
    heffte::fft3d_r2c<heffte::backend::rocfft> fft
#else
    heffte::fft3d_r2c<heffte::backend::fftw> fft
#endif
        ({{local_box.smallEnd(0),local_box.smallEnd(1),local_box.smallEnd(2)},
          {local_box.bigEnd(0)  ,local_box.bigEnd(1)  ,local_box.bigEnd(2)}},
         {{c_local_box.smallEnd(0),c_local_box.smallEnd(1),c_local_box.smallEnd(2)},
          {c_local_box.bigEnd(0)  ,c_local_box.bigEnd(1)  ,c_local_box.bigEnd(2)}},
         0, ParallelDescriptor::Communicator());

    using heffte_complex = typename heffte::fft_output<Real>::type;

    fft.forward(fab.dataPtr(), (heffte_complex*)cfab.dataPtr());
    fft.backward((heffte_complex*)cfab.dataPtr(), fab.dataPtr());

    Gpu::synchronize();    
    double t0 = amrex::second();

    for (int itest = 0; itest < ntests; ++itest) {
        fft.forward(fab.dataPtr(), (heffte_complex*)cfab.dataPtr());
        fft.backward((heffte_complex*)cfab.dataPtr(), fab.dataPtr());
    }

    Gpu::synchronize();
    double t1 = amrex::second();

    return (t1-t0) / double(ntests);
}
#endif

int main (int argc, char* argv[])
{
    static_assert(AMREX_SPACEDIM == 3);

    amrex::Initialize(argc, argv);
    {
        BL_PROFILE("main");

        AMREX_D_TERM(int n_cell_x = 256;,
                     int n_cell_y = 256;,
                     int n_cell_z = 256);

        {
            ParmParse pp;
            AMREX_D_TERM(pp.query("n_cell_x", n_cell_x);,
                         pp.query("n_cell_y", n_cell_y);,
                         pp.query("n_cell_z", n_cell_z));
        }

        amrex::Print() << "\n FFT size: " << n_cell_x << " " << n_cell_y << " " << n_cell_z << " "
                       << "  # of proc. " << ParallelDescriptor::NProcs() << "\n\n";

        Box domain(IntVect(0),IntVect(n_cell_x-1,n_cell_y-1,n_cell_z-1));
        BoxArray ba = amrex::decompose(domain, ParallelDescriptor::NProcs(), {true,true,true});
        AMREX_ALWAYS_ASSERT(ba.size() == ParallelDescriptor::NProcs());
        DistributionMapping dm = FFT::detail::make_iota_distromap(ba.size());

        GpuArray<Real,3> dx{1._rt/Real(n_cell_x), 1._rt/Real(n_cell_y), 1._rt/Real(n_cell_z)};

        MultiFab mf(ba, dm, 1, 0);
        auto const& ma = mf.arrays();
        ParallelFor(mf, [=] AMREX_GPU_DEVICE (int b, int i, int j, int k)
        {
            AMREX_D_TERM(Real x = (i+0.5_rt) * dx[0] - 0.5_rt;,
                         Real y = (j+0.5_rt) * dx[1] - 0.5_rt;,
                         Real z = (k+0.5_rt) * dx[2] - 0.5_rt);
            ma[b](i,j,k) = std::exp(-10._rt*
                (AMREX_D_TERM(x*x*1.05_rt, + y*y*0.90_rt, + z*z)));
        });
        Gpu::streamSynchronize();

        Box cdomain(IntVect(0), IntVect(n_cell_x/2+1, n_cell_y-1, n_cell_z-1));
        BoxArray cba = amrex::decompose(cdomain, ParallelDescriptor::NProcs(), {true,true,true});
        AMREX_ALWAYS_ASSERT(cba.size() == ParallelDescriptor::NProcs());

        cMultiFab cmf(cba, dm, 1, 0);

        auto t_amrex_pencil = test_amrex_pencil(domain, mf, cmf);
        auto t_amrex_plane = test_amrex_plane(domain, mf, cmf);
        amrex::Print() << "  armex pencil time: " << t_amrex_pencil << "\n"
                       << "  amrex plane  time: " << t_amrex_plane << "\n";

#ifdef USE_HEFFTE
        auto t_heffte = test_heffte(domain, mf, cmf);
        amrex::Print() << "  heffte       time: " << t_heffte << "\n";
#endif

        amrex::Print() << "\n";
    }
    amrex::Finalize();
}