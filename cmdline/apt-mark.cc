// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
/* #####################################################################
   apt-mark - show and change auto-installed bit information
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/cachefile.h>
#include <apt-pkg/cacheset.h>
#include <apt-pkg/cmndline.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/statechanges.h>
#include <apt-pkg/cacheiterators.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/depcache.h>
#include <apt-pkg/macros.h>
#include <apt-pkg/pkgcache.h>

#include <apt-private/private-cmndline.h>
#include <apt-private/private-output.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <apti18n.h>
									/*}}}*/
using namespace std;

/* DoAuto - mark packages as automatically/manually installed		{{{*/
static bool DoAuto(CommandLine &CmdL)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   pkgDepCache *DepCache = CacheFile.GetDepCache();
   if (unlikely(Cache == NULL || DepCache == NULL))
      return false;

   APT::PackageList pkgset = APT::PackageList::FromCommandLine(CacheFile, CmdL.FileList + 1);
   if (pkgset.empty() == true)
      return _error->Error(_("No packages found"));

   bool MarkAuto = strcasecmp(CmdL.FileList[0],"auto") == 0;
   int AutoMarkChanged = 0;

   for (APT::PackageList::const_iterator Pkg = pkgset.begin(); Pkg != pkgset.end(); ++Pkg)
   {
      if (Pkg->CurrentVer == 0)
      {
	 ioprintf(c1out,_("%s can not be marked as it is not installed.\n"), Pkg.FullName(true).c_str());
	 continue;
      }
      else if ((((*DepCache)[Pkg].Flags & pkgCache::Flag::Auto) == pkgCache::Flag::Auto) == MarkAuto)
      {
	 if (MarkAuto == false)
	    ioprintf(c1out,_("%s was already set to manually installed.\n"), Pkg.FullName(true).c_str());
	 else
	    ioprintf(c1out,_("%s was already set to automatically installed.\n"), Pkg.FullName(true).c_str());
	 continue;
      }

      if (MarkAuto == false)
	 ioprintf(c1out,_("%s set to manually installed.\n"), Pkg.FullName(true).c_str());
      else
	 ioprintf(c1out,_("%s set to automatically installed.\n"), Pkg.FullName(true).c_str());

      DepCache->MarkAuto(Pkg, MarkAuto);
      ++AutoMarkChanged;
   }
   if (AutoMarkChanged > 0 && _config->FindB("APT::Mark::Simulate", false) == false)
      return DepCache->writeStateFile(NULL);
   return true;
}
									/*}}}*/
/* DoMarkAuto - mark packages as automatically/manually installed	{{{*/
/* Does the same as DoAuto but tries to do it exactly the same why as
   the python implementation did it so it can be a drop-in replacement */
static bool DoMarkAuto(CommandLine &CmdL)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   pkgDepCache *DepCache = CacheFile.GetDepCache();
   if (unlikely(Cache == NULL || DepCache == NULL))
      return false;

   APT::PackageList pkgset = APT::PackageList::FromCommandLine(CacheFile, CmdL.FileList + 1);
   if (pkgset.empty() == true)
      return _error->Error(_("No packages found"));

   bool const MarkAuto = strcasecmp(CmdL.FileList[0],"markauto") == 0;
   bool const Verbose = _config->FindB("APT::MarkAuto::Verbose", false);
   int AutoMarkChanged = 0;

   for (APT::PackageList::const_iterator Pkg = pkgset.begin(); Pkg != pkgset.end(); ++Pkg)
   {
      if (Pkg->CurrentVer == 0 ||
	  (((*DepCache)[Pkg].Flags & pkgCache::Flag::Auto) == pkgCache::Flag::Auto) == MarkAuto)
	 continue;

      if (Verbose == true)
	 ioprintf(c1out, "changing %s to %d\n", Pkg.Name(), (MarkAuto == false) ? 0 : 1);

      DepCache->MarkAuto(Pkg, MarkAuto);
      ++AutoMarkChanged;
   }
   if (AutoMarkChanged > 0 && _config->FindB("APT::Mark::Simulate", false) == false)
      return DepCache->writeStateFile(NULL);

   _error->Notice(_("This command is deprecated. Please use 'apt-mark auto' and 'apt-mark manual' instead."));

   return true;
}
									/*}}}*/
/* ShowAuto - show automatically installed packages (sorted)		{{{*/
static bool ShowAuto(CommandLine &CmdL)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   pkgDepCache *DepCache = CacheFile.GetDepCache();
   if (unlikely(Cache == NULL || DepCache == NULL))
      return false;

   std::vector<string> packages;

   bool const ShowAuto = strcasecmp(CmdL.FileList[0],"showauto") == 0;

   if (CmdL.FileList[1] == 0)
   {
      packages.reserve(Cache->HeaderP->PackageCount / 3);
      for (pkgCache::PkgIterator P = Cache->PkgBegin(); P.end() == false; ++P)
	 if (P->CurrentVer != 0 &&
	     (((*DepCache)[P].Flags & pkgCache::Flag::Auto) == pkgCache::Flag::Auto) == ShowAuto)
	    packages.push_back(P.FullName(true));
   }
   else
   {
      APT::CacheSetHelper helper(false); // do not show errors
      APT::PackageSet pkgset = APT::PackageSet::FromCommandLine(CacheFile, CmdL.FileList + 1, helper);
      packages.reserve(pkgset.size());
      for (APT::PackageSet::const_iterator P = pkgset.begin(); P != pkgset.end(); ++P)
	 if (P->CurrentVer != 0 &&
	     (((*DepCache)[P].Flags & pkgCache::Flag::Auto) == pkgCache::Flag::Auto) == ShowAuto)
	    packages.push_back(P.FullName(true));
   }

   std::sort(packages.begin(), packages.end());

   for (vector<string>::const_iterator I = packages.begin(); I != packages.end(); ++I)
      std::cout << *I << std::endl;

   return true;
}
									/*}}}*/
/* DoHold - mark packages as hold by dpkg				{{{*/
static bool DoHold(CommandLine &CmdL)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   if (unlikely(Cache == NULL))
      return false;

   APT::VersionVector pkgset = APT::VersionVector::FromCommandLine(CacheFile, CmdL.FileList + 1, APT::CacheSetHelper::INSTCAND);
   if (pkgset.empty() == true)
      return _error->Error(_("No packages found"));

   bool const MarkHold = strcasecmp(CmdL.FileList[0],"hold") == 0;

   auto const part = std::stable_partition(pkgset.begin(), pkgset.end(),
        [](pkgCache::VerIterator const &V) { return V.ParentPkg()->SelectedState == pkgCache::State::Hold; });

   auto const doneBegin = MarkHold ? pkgset.begin() : part;
   auto const doneEnd = MarkHold ? part : pkgset.end();

   std::for_each(doneBegin, doneEnd, [&MarkHold](pkgCache::VerIterator const &V) {
      if (MarkHold == true)
        ioprintf(c1out, _("%s was already set on hold.\n"), V.ParentPkg().FullName(true).c_str());
      else
        ioprintf(c1out, _("%s was already not hold.\n"), V.ParentPkg().FullName(true).c_str());
   });

   if (doneBegin == pkgset.begin() && doneEnd == pkgset.end())
      return true;

   auto const changeBegin = MarkHold ? part : pkgset.begin();
   auto const changeEnd = MarkHold ? pkgset.end() : part;

   APT::StateChanges marks;
   std::move(changeBegin, changeEnd, std::back_inserter(MarkHold ? marks.Hold() : marks.Unhold()));
   pkgset.clear();

   bool success = true;
   if (_config->FindB("APT::Mark::Simulate", false) == false)
   {
      success = marks.Save();
      if (success == false)
	 _error->Error(_("Executing dpkg failed. Are you root?"));
   }

   for (auto Ver : marks.Hold())
      ioprintf(c1out,_("%s set on hold.\n"), Ver.ParentPkg().FullName(true).c_str());
   for (auto Ver : marks.Unhold())
      ioprintf(c1out,_("Canceled hold on %s.\n"), Ver.ParentPkg().FullName(true).c_str());

   return success;
}
									/*}}}*/
/* ShowHold - show packages set on hold in dpkg status			{{{*/
static bool ShowHold(CommandLine &CmdL)
{
   pkgCacheFile CacheFile;
   pkgCache *Cache = CacheFile.GetPkgCache();
   if (unlikely(Cache == NULL))
      return false;

   std::vector<string> packages;

   if (CmdL.FileList[1] == 0)
   {
      packages.reserve(50); // how many holds are realistic? I hope just a few…
      for (pkgCache::PkgIterator P = Cache->PkgBegin(); P.end() == false; ++P)
	 if (P->SelectedState == pkgCache::State::Hold)
	    packages.push_back(P.FullName(true));
   }
   else
   {
      APT::CacheSetHelper helper(false); // do not show errors
      APT::PackageSet pkgset = APT::PackageSet::FromCommandLine(CacheFile, CmdL.FileList + 1, helper);
      packages.reserve(pkgset.size());
      for (APT::PackageSet::const_iterator P = pkgset.begin(); P != pkgset.end(); ++P)
	 if (P->SelectedState == pkgCache::State::Hold)
	    packages.push_back(P.FullName(true));
   }

   std::sort(packages.begin(), packages.end());

   for (vector<string>::const_iterator I = packages.begin(); I != packages.end(); ++I)
      std::cout << *I << std::endl;

   return true;
}
									/*}}}*/
// ShowHelp - Show a help screen					/*{{{*/
// ---------------------------------------------------------------------
/* */
static bool ShowHelp(CommandLine &)
{
   ioprintf(std::cout, "%s %s (%s)\n", PACKAGE, PACKAGE_VERSION, COMMON_ARCH);

   cout <<
    _("Usage: apt-mark [options] {auto|manual} pkg1 [pkg2 ...]\n"
      "\n"
      "apt-mark is a simple command line interface for marking packages\n"
      "as manually or automatically installed. It can also list marks.\n"
      "\n"
      "Commands:\n"
      "   auto - Mark the given packages as automatically installed\n"
      "   manual - Mark the given packages as manually installed\n"
      "   hold - Mark a package as held back\n"
      "   unhold - Unset a package set as held back\n"
      "   showauto - Print the list of automatically installed packages\n"
      "   showmanual - Print the list of manually installed packages\n"
      "   showhold - Print the list of package on hold\n"
      "\n"
      "Options:\n"
      "  -h  This help text.\n"
      "  -q  Loggable output - no progress indicator\n"
      "  -qq No output except for errors\n"
      "  -s  No-act. Just prints what would be done.\n"
      "  -f  read/write auto/manual marking in the given file\n"
      "  -c=? Read this configuration file\n"
      "  -o=? Set an arbitrary configuration option, eg -o dir::cache=/tmp\n"
      "See the apt-mark(8) and apt.conf(5) manual pages for more information.")
      << std::endl;
   return true;
}
									/*}}}*/
int main(int argc,const char *argv[])					/*{{{*/
{
   CommandLine::Dispatch Cmds[] = {{"help",&ShowHelp},
				   {"auto",&DoAuto},
				   {"manual",&DoAuto},
				   {"hold",&DoHold},
				   {"unhold",&DoHold},
				   {"showauto",&ShowAuto},
				   {"showmanual",&ShowAuto},
				   {"showhold",&ShowHold},
				   // be nice and forgive the typo
				   {"showholds",&ShowHold},
				   // be nice and forgive it as it is technical right
				   {"install",&DoHold},
				   // obsolete commands for compatibility
				   {"markauto", &DoMarkAuto},
				   {"unmarkauto", &DoMarkAuto},
                                   {0,0}};

   std::vector<CommandLine::Args> Args = getCommandArgs("apt-mark", CommandLine::GetCommand(Cmds, argc, argv));

   // Set up gettext support
   setlocale(LC_ALL,"");
   textdomain(PACKAGE);

   CommandLine CmdL;
   ParseCommandLine(CmdL, Cmds, Args.data(), &_config, &_system, argc, argv, ShowHelp);

   InitOutput();

   // Match the operation
   CmdL.DispatchArg(Cmds);

   // Print any errors or warnings found during parsing
   bool const Errors = _error->PendingError();
   if (_config->FindI("quiet",0) > 0)
      _error->DumpErrors();
   else
      _error->DumpErrors(GlobalError::DEBUG);
   return Errors == true ? 100 : 0;
}
									/*}}}*/
