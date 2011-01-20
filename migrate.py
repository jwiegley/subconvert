#!/usr/bin/env python
# coding: utf-8

import os
import sys
import string
import cStringIO

from subprocess import Popen, PIPE

sys.path.append("/Users/johnw/src/boost-migrate/svndumptool")
import svndump

verbose = True

authors = {
    "(no author)":     ("Douglas Gregor",             "doug~gregor<>gmail~com"),
    "aaron_windsor":   ("Aaron Windsor",              "aaron~windsor<>gmail~com"),
    "abrahams":        ("Dave Abrahams",              "dave<>boostpro~com"),
    "agurtovoy":       ("Aleksey Gurtovoy",           "agurtovoy<>meta-comm~com"),
    "akalin":          ("Frederick Akalin",           "akalin<>akalin~cx"),
    "aliazarbayejani": ("Ali J~ Azarbayejani",        "ali<>merl~com"),
    "alisdairm27386":  ("Alisdair Meredith",          "alisdair~meredith<>uk~renaultf1~com"),
    "alkise":          ("Alkis Evlogimenos",          "alkis<>routescience~com"),
    "alnsn":           ("Alexander Nasonov",          "alnsn<>yandex~ru"),
    "andreas_huber69": ("Andreas Huber",              "andreas_huber69<>users~sourceforge~net"),
    "andysem":         ("Andrey Semashev",            "andrey~semashev<>gmail~com"),
    "anthonyw":        ("Anthony Williams",           "anthony~ajw<>gmail~com"),
    "asutton":         ("Andrew Sutton",              "andrew~n~sutton<>gmail~com"),
    "az_sw_dude":      ("Jeff Garland",               "jeff<>crystalclearsoftware~com"),
    "beman":           ("Beman Dawes",                "bdawes<>acm~org"),
    "beman_dawes":     ("Beman Dawes",                "bdawes<>acm~org"),
    "bemandawes":      ("Beman Dawes",                "bdawes<>acm~org"),
    "ben_hanson":      ("Ben Hanson",                 "jamin~hanson<>googlemail~com"),
    "bgubenko":        ("Boris Gubenko",              "boris~gubenko<>hp~com"),
    "bill_kempf":      ("William E~ Kempf",           "wekempf<>cox~net"),
    "biochimia":       ("João Abecasis",              "jpabecasis<>zmail~pt"),
    "bjorn_karlsson":  ("Björn Karlsson",             "Bjorn~Karlsson<>readsoft~com"),
    "blq":             ("Fredrik Blomqvist",          "fredrik_blomqvist<>home~se"),
    "boost_admin":     ("Boost Administrator",        "boost_admin<>users~sourceforge~net"),
    "brett_calcott":   ("Brett Calcott",              "brett~calcott<>gmail~com"),
    "burbelgruff":     ("Peder Holt",                 "peder~holt<>gmail~com"),
    "carlos_p_coelho": ("Carlos Pinto Coelho",        "cfspc<>altrabroadband~com"),
    "cepstein":        ("Caleb Epstein",              "caleb~epstein<>gmail~com"),
    "cgc":             ("Christopher Currie",         "christopher<>currie~com"),
    "chris_kohlhoff":  ("Christopher M~ Kohlhoff",    "chris<>kohlhoff~com"),
    "cmb537":          ("cbarrron",                   "cmb537<>users~sourceforge~net"),
    "cnewbold":        ("Chris Newbold",              "Chris~Newbold<>mathworks~com"),
    "cornedbee":       ("Sebastian Redl",             "sebastian~redl<>getdesigned~at"),
    "cpdaniel":        ("Carl Daniel",                "cpdaniel<>mvps~org"),
    "cppljevans":      ("Larry Evans",                "cppljevans<>cox-internet~com"),
    "cslittle":        ("Chris Little",               "cslittle<>mac~com"),
    "dan_marsden":     ("Dan Marsden",                "danmarsden<>yahoo~co~uk"),
    "daniel_egloff":   ("Daniel Egloff",              "daniel~egloff<>zkb~ch"),
    "daniel_frey":     ("Daniel Frey",                "d~frey<>gmx~de"),
    "daniel_wallin":   ("Daniel Wallin",              "daniel<>boostpro~com"),
    "danieljames":     ("Daniel James",               "dnljms<>gmail~com"),
    "danielw":         ("Daniel Wallin",              "daniel<>boostpro~com"),
    "danmarsden":      ("Dan Marsden",                "danmarsden<>yahoo~co~uk"),
    "darinadler":      ("Darin Adler",                "darin<>bentspoon~com"),
    "dave":            ("Dave Abrahams",              "dave<>boostpro~com"),
    "davedeakins":     ("David Deakins",              "ddeakins<>veeco~com"),
    "david_abrahams":  ("Dave Abrahams",              "dave<>boostpro~com"),
    "dfrey42":         ("Daniel Frey",                "d~frey<>gmx~de"),
    "dgregor":         ("Douglas Gregor",             "doug~gregor<>gmail~com"),
    "diemumiee":       ("Andreas Pokorny",            "andreas~pokorny<>gmail~com"),
    "djenkins":        ("David Jenkins",              "david<>jenkins~net"),
    "djowel":          ("Joel de Guzman",             "djowel<>gmail~com"),
    "dlwalker":        ("Daryle L~ Walker",           "darylew<>hotmail~com"),
    "dramatec":        ("Jim Douglas",                "jim<>dramatec~co~uk"),
    "ebf":             ("Eric Friedman",              "ebf<>users~sourceforge~net"),
    "emildotchevski":  ("Emil Dotchevski",            "emil<>revergestudios~com"),
    "eric_niebler":    ("Eric Niebler",               "eric<>boostpro~com"),
    "fbarel":          ("François Barel",             "frabar666<>gmail~com"),
    "fcacciola":       ("Fernando Cacciola",          "fernando~cacciola<>gmail~com"),
    "fmhess":          ("Frank Mori Hess",            "fmhess<>speakeasy~net"),
    "ganssauge":       ("Gottfried Ganßauge",         "gottfried~ganssauge<>haufe~de"),
    "garcia":          ("Ronald Garcia",              "rxg<>cs~cmu~edu"),
    "gennaro_prota":   ("Gennaro Prota",              "gennaro~prota<>yahoo~com"),
    "giovannibajo":    ("Giovanni Bajo",              "giovannibajo<>libero~it"),
    "glassfordm":      ("Michael Glassford",          "glassfordm<>hotmail~com"),
    "gmelquio":        ("Guillaume Melquiond",        "guillaume~melquiond<>ens-lyon~fr"),
    "grafik":          ("René Rivera",                "grafik<>redshift-software~com"),
    "guwi17":          ("Gunter Winkler",             "guwi17<>gmx~de"),
    "gwpowell":        ("Gary Powell",                "powellg<>amazon~com"),
    "hervebronnimann": ("Hervé Brönnimann",           "hervebronnimann<>mac~com"),
    "hevad57":         ("David Hawkes",               "hevad57<>users~sourceforge~net"),
    "hkaiser":         ("Hartmut Kaiser",             "hartmut~kaiser<>gmail~com"),
    "hljin":           ("Hailin Jin",                 "hljin<>adobe~com"),
    "hubert_holin":    ("Hubert Holin",               "Hubert~Holin<>lmd~polytechnique~fr"),
    "igaztanaga":      ("Ion Gaztañaga",              "igaztanaga<>gmail~com"),
    "imaman":          ("Itay Maman",                 "itay_maman<>yahoo~com"),
    "imikejackson":    ("Mike Jackson",               "mike~jackson<>bluequartz~net"),
    "jano_gaspar":     ("Jan Gaspar",                 "jano_gaspar<>yahoo~com"),
    "jbandela":        ("John R~ Bandela",            "jbandela<>ufl~edu"),
    "jbrandmeyer":     ("Jonathan Brandmeyer",        "jbrandmeyer<>earthlink~net"),
    "jdmoore99":       ("John D~ Moore",              "jdmoore99<>users~sourceforge~net"),
    "jeffflinn":       ("Jeff Flinn",                 "TriumphSprint2000<>hotmail~com"),
    "jerrydy":         ("Jerry Dy",                   "jd2419<>sbc~com"),
    "jewillco":        ("Jeremiah J~ Willcock",       "jewillco<>osl~iu~edu"),
    "jhunold":         ("Jürgen Hunold",              "juergen~hunold<>ivembh~de"),
    "jmaurer":         ("Jens Maurer",                "Jens~Maurer<>gmx~net"),
    "joaquin":         ("Joaquín M~ López Muñoz",     "joaquin<>tid~es"),
    "joaquintides":    ("Joaquín M~ López Muñoz",     "joaquin<>tid~es"),
    "joerg_walter":    ("Jörg Walter",                "jhr~walter<>t-online~de"),
    "johnmaddock":     ("John Maddock",               "john<>johnmaddock~co~uk"),
    "jonkalb":         ("Jon Kalb",                   "jonkalb<>microsoft~com"),
    "joseph~gauterin": ("Joseph Gauterin",            "joseph~gauterin<>googlemail~com"),
    "jsiek":           ("Jeremy Siek",                "jsiek<>osl~iu~edu"),
    "jsuter":          ("Jaap Suter",                 "boost<>jaapsuter~com"),
    "jurko":           ("Jurko Gospodnetić",          "jurko~gospodnetic<>docte~hr"),
    "kaalus":          ("Marcin Kalicinski",          "kalita<>poczta~onet~pl"),
    "kevlin":          ("Kevlin Henney",              "kevlin<>curbralan~com"),
    "latte":           ("Terence Wilson",             "tez<>latte~com"),
    "lbourdev":        ("Lubomir Bourdev",            "lbourdev<>adobe~com"),
    "llee1":           ("Lie-Quan Lee",               "liequan<>slac~stanford~edu"),
    "mark_rodgers":    ("Mark Rodgers",               "mark~rodgers<>cadenza~co~nz"),
    "marshall":        ("Marshall Clow",              "marshall<>idio~com"),
    "martin_wille":    ("Martin Wille",               "mw8329<>yahoo~com~au"),
    "mathiaskoch":     ("Mathias Koch",               "mathiaskoch<>users~sourceforge~net"),
    "matias":          ("Matias Capeletto",           "matias~capeletto<>gmail~com"),
    "matiascape":      ("Matias Capeletto",           "matias~capeletto<>gmail~com"),
    "matt_calabrese":  ("Matthew Calabrese",          "rivorus<>gmail~com"),
    "matthiasschabel": ("Matthias Christian Schabel", "boost<>schabel-family~org"),
    "mbergal":         ("Misha Bergal",               "mbergal<>meta-comm~com"),
    "mclow":           ("Marshall Clow",              "marshall<>idio~com"),
    "memring":         ("Pavol Droba",                "droba<>topmail~sk"),
    "mistevens":       ("Michael Stevens",            "mail<>michael-stevens~de"),
    "mmurrett":        ("Mac Murrett",                "mmurrett<>mac~com"),
    "mrovner":         ("Mike Rovner",                "mike<>bindkey~com"),
    "msclrhd":         ("Reece H~ Dunn",              "msclrhd<>hotmail~com"),
    "nasonov":         ("Alexander Nasonov",          "alnsn<>yandex~ru"),
    "nbecker":         ("Neal D~ Becker",             "ndbecker2<>gmail~com"),
    "nesotto":         ("Thorsten Jørgen Ottosen",    "nesotto<>cs~auc~dk"),
    "ngedmond":        ("Nick Edmonds",               "ngedmond<>cs~indiana~edu"),
    "nicoddemus":      ("Bruno da Silva de Oliveira", "nicodemus<>globalite~com~br"),
    "niels_dekker":    ("Niels Dekker",               "dekkerware<>xs4all~nl"),
    "nielsdekker":     ("Niels Dekker",               "dekkerware<>xs4all~nl"),
    "nikiml":          ("Nikolay Mladenov",           "nikolay~mladenov<>gmail~com"),
    "nimnul":          ("Andrey Melnikov",            "melnikov<>simplexsoft~com"),
    "nksauter":        ("Nicholas K~ Sauter",         "nksauter<>lbl~gov"),
    "nmotgi":          ("Nitin Jeevan Motgi",         "nitin~motgi<>gmail~com"),
    "nmusatti":        ("Nicola Musatti",             "Nicola~Musatti<>gmail~com"),
    "noel_belcourt":   ("K~ Noel Belcourt",           "kbelco<>sandia~gov"),
    "nuffer":          ("Daniel Nuffer",              "dan-boost<>nuffer~name"),
    "pavol_droba":     ("Pavol Droba",                "droba<>topmail~sk"),
    "pdimov":          ("Peter Dimov",                "pdimov<>mmltd~net"),
    "pedro_ferreira":  ("Pedro Ferreira",             "pedro~ferreira<>mog-solutions~com"),
    "pmenso57":        ("Paul Mensonides",            "pmenso57<>comcast~net"),
    "pvozenilek":      ("Pavel Vozenilek",            "pavel_vozenilek<>hotmail~com"),
    "ramey":           ("Robert Ramey",               "ramey<>rrsd~com"),
    "raoulgough":      ("Raoul Gough",                "RaoulGough<>yahoo~co~uk"),
    "redi":            ("Jonathan Wakely",            "cow<>compsoc~man~ac~uk"),
    "reportbase":      ("Tom Brinkman",               "reportbase<>gmail~com"),
    "reportbase2004":  ("Tom Brinkman",               "reportbase<>gmail~com"),
    "rgarcia":         ("Ronald Garcia",              "rxg<>cs~cmu~edu"),
    "rogeeff":         ("Gennadiy Rozental",          "rogeeff<>gmail~com"),
    "root":            ("Boost Administrator",        "boost_admin<>users~sourceforge~net"),
    "rwgk":            ("Ralf W~ Grosse-Kunstleve",   "rwgk<>yahoo~com"),
    "samuel_k":        ("Samuel Krempp",              "krempp<>crans~ens-cachan~fr"),
    "samuel_krempp":   ("Samuel Krempp",              "krempp<>crans~ens-cachan~fr"),
    "schoepflin":      ("Markus Schöpflin",           "markus~schoepflin<>comsoft~de"),
    "sdiederich":      ("Stephan Diederich",          "stephan~diederich<>googlemail~com"),
    "shammah":         ("Stephen Cleary",             "scleary<>jerviswebb~com"),
    "siliconman":      ("David Dean",                 "siliconman<>spamcop~net"),
    "slapi":           ("Stefan Slapeta",             "stefan<>slapeta~com"),
    "sohail":          ("Sohail Somani",              "sohail<>taggedtype~net"),
    "speedsnail":      ("Roland Schwarz",             "roland~schwarz<>chello~at"),
    "srajko":          ("Stjepan Rajko",              "stjepan~rajko<>gmail~com"),
    "stefan":          ("Stefan Seefeld",             "seefeld<>sympatico~ca"),
    "steven_watanabe": ("Steven Watanabe",            "watanabesj<>gmail~com"),
    "straszheim":      ("Troy D~ Straszheim",         "troy<>resophonic~com"),
    "syl":             ("Sylvain Pion",               "Sylvain~Pion<>sophia~inria~fr"),
    "t_schwinger":     ("Tobias Schwinger",           "tschwinger<>isonews2~com"),
    "thejcab":         ("Juan Carlos Arevalo-Baeza",  "jcab<>JCABs-Rumblings~com"),
    "tknapen":         ("Toon Knapen",                "toon~knapen<>fft~be"),
    "troy":            ("Troy D~ Straszheim",         "troy<>resophonic~com"),
    "troyer":          ("Matthias Troyer",            "troyer<>phys~ethz~ch"),
    "tslettebo":       ("Terje Slettebø",             "tslettebo<>broadpark~no"),
    "turkanis":        ("Jonathan Turkanis",          "turkanis<>coderage~com"),
    "tzlaine":         ("Zach Laine",                 "whatwasthataddress<>gmail~com"),
    "uid30600":        ("uid30600",                   "uid30600<>boost~org"),
    "uid30850":        ("uid30850",                   "uid30850<>boost~org"),
    "ullrich_koethe":  ("Ullrich Köthe",              "koethe<>informatik~uni-hamburg~de"),
    "urzuga":          ("Jaakko Järvi",               "jarvi<>cs~tamu~edu"),
    "vawjr":           ("Victor A~ Wagner Jr~",       "vawjr<>rudbek~com"),
    "vertleyb":        ("Arkadiy Vertleyb",           "vertleyb<>hotmail~com"),
    "vesa_karvonen":   ("Vesa Karvonen",              "vesa_karvonen<>hotmail~com"),
    "vladimir_prus":   ("Vladimir Prus",              "vladimir<>codesourcery~com"),
    "witt":            ("Thomas Witt",                "witt<>acm~org"),
    "wom-work":        ("Ben Hutchings",              "ben<>decadentplace~org~uk"),
}

def git(cmd, *args, **kwargs):
    restart = True
    while restart:
        stdin_mode = None
        if 'input' in kwargs:
            stdin_mode = PIPE

        if verbose:
            print "=> git %s %s" % (cmd, string.join(args, ' '))
            if 'input' in kwargs:
                print "<<EOF"
                print kwargs['input'],
                print "EOF"

        environ = os.environ.copy()
        
        if 'author_name' in kwargs and kwargs['author_name']:
            environ['GIT_AUTHOR_NAME']     = kwargs['author_name']
        if 'author_email' in kwargs and kwargs['author_email']:
            environ['GIT_AUTHOR_EMAIL']    = kwargs['author_email']
        if 'author_date' in kwargs and kwargs['author_date']:
            environ['GIT_AUTHOR_DATE']     = kwargs['author_date']
        if 'committer_name' in kwargs and kwargs['committer_name']:
            environ['GIT_COMMITTER_NAME']  = kwargs['committer_name']
        if 'committer_email' in kwargs and kwargs['committer_email']:
            environ['GIT_COMMITTER_EMAIL'] = kwargs['committer_email']
        if 'committer_date' in kwargs and kwargs['committer_date']:
            environ['GIT_COMMITTER_DATE']  = kwargs['committer_date']
        if 'email' in kwargs and kwargs['email']:
            environ['EMAIL']               = kwargs['email']

        if 'repository' in kwargs:
            environ['GIT_DIR'] = kwargs['repository']

            git_dir = environ['GIT_DIR']
            if not os.path.isdir(git_dir):
                proc = Popen(('git', 'init'), env = environ,
                             stdout = PIPE, stderr = PIPE)
                if proc.wait() != 0:
                    raise GitError('init', [], {}, proc.stderr.read())

        if 'worktree' in kwargs:
            environ['GIT_WORK_TREE'] = kwargs['worktree']
            work_tree = environ['GIT_WORK_TREE']
            if not os.path.isdir(work_tree):
                os.makedirs(work_tree)

        proc = Popen(('git', cmd) + args, env = environ,
                     stdin  = stdin_mode, stdout = PIPE, stderr = PIPE)

        if 'input' in kwargs:
            input = kwargs['input']
        else:
            input = ''
       
        out, err = proc.communicate(input) 

        returncode = proc.returncode
        restart = False
        ignore_errors = 'ignore_errors' in kwargs and kwargs['ignore_errors']
        if returncode != 0:
            if 'restart' in kwargs:
                if kwargs['restart'](cmd, args, kwargs):
                    restart = True
            elif not ignore_errors:
                raise GitError(cmd, args, kwargs, err)

    if not 'ignore_output' in kwargs:
        if 'keep_newline' in kwargs:
            return out
        else:
            return out[:-1]

class GitBlob:
    sha1       = None
    name       = None
    executable = False
    data       = None

    def __init__(self, name, data, executable = False):
        self.name       = name
        self.data       = data
        self.executable = executable

    def ls(self):
        assert(self.sha1)
        return "%s blob %s\t%s" % ("100755" if self.executable else "100644",
                                   self.sha1, self.name)

    def write(self):
        self.sha1 = git('hash-object', '-w', '--stdin', '--no-filters',
                        input = self.data)

class GitTree:
    sha1    = None
    name    = None
    entries = None

    def __init__(self, name):
        self.name    = name
        self.entries = {}

    def ls(self):
        assert(self.sha1)
        return "040000 tree %s\t%s" % (self.sha1, self.name)

    def update(self, path, obj):
        self.sha1 = None
        if verbose: print "tree.add:", path, obj

        segments = path.split('/')
        if len(segments) == 1:
            self.entries[segments[0]] = obj
        else:
            if segments[0] not in self.entries:
                self.entries[segments[0]] = GitTree(segments[0])
            self.entries[segments[0]].update(string.join(segments[1:], '/'), obj)

    def remove(self, path):
        self.sha1 = None
        if verbose: print "tree.remove:", path

        segments = path.split('/')
        assert segments[0] in self.entries
        if len(segments) == 1:
            del self.entries[segments[0]]
        else:
            self.entries[segments[0]].remove(string.join(segments[1:], '/'))

    def write(self):
        if self.sha1: return

        table = cStringIO.StringIO()
        for entry in self.entries.values():
            entry.write()
            table.write(entry.ls())
            table.write('\0')

        self.sha1 = git('mktree', '-z', input = table.getvalue())
        del table

class GitCommit:
    sha1            = None
    parents         = []
    tree            = None
    author_name     = None
    author_email    = None
    author_date     = None
    committer_name  = None
    committer_email = None
    committer_date  = None
    comment         = None
    name            = None

    def __init__(self, parents = [], name = None):
        self.parents = parents
        self.name    = name

    def fork(self):
        new_commit = GitCommit(parents = [self.sha1])
        new_commit.tree = self.tree
        return new_commit

    def ls(self):
        assert(self.sha1)
        assert(self.name)
        return "160000 commit %s\t%s" % (self.sha1, self.name)

    def update(self, path, obj):
        self.sha1 = None
        if verbose: print "commit.add:", path, obj
        if not self.tree:
            self.tree = GitTree(path.split('/')[0])
        self.tree.update(path, obj)

    def remove(self, path):
        self.sha1 = None
        if verbose: print "commit.remove:", path
        assert self.tree
        self.tree.remove(path)

    def write(self, comment):
        if self.sha1: return
        self.comment = comment

        assert(self.tree)
        self.tree.write()

        args = ['commit-tree', self.tree.sha1]
        for parent in self.parents:
            args.append('-p')
            args.append(parent)

        self.sha1 = git(*args, input = comment,
                         author_name     = self.author_name,
                         author_email    = self.author_email,
                         author_date     = self.author_date,
                         committer_name  = self.committer_name  or self.author_name,
                         committer_email = self.committer_email or self.author_email,
                         committer_date  = self.committer_date  or self.author_date)

class GitBranch:
    name = None

    def __init__(self, name = 'master'):
        self.name = name

    def update(self, commit):
        git('update-ref', 'refs/heads/%s' % self.name, commit.sha1)

def read_dumpfile(path):
    dump = svndump.file.SvnDumpFile()
    dump.open(path)

    while dump.read_next_rev():

        txn = 1
        for node in dump.get_nodes_iter():
            path   = node.get_path()
            kind   = node.get_kind()
            action = node.get_action()

            #if action == "add":
            #    assert path not in entities
            #    entities[path] = kind
            #    if kind == "dir" and node.has_copy_from():
            #        #print "copy directory %s -> %s" % (node.get_copy_from_path(), path)
            #        for key, value in entities.items():
            #            if key.startswith(node.get_copy_from_path() + "/"):
            #                newpath = path + key[len(node.get_copy_from_path()) :]
            #                #print "copy entry %s -> %s" % (key, newpath)
            #                entities[newpath] = value
            #elif action == "delete":
            #    assert path in entities
            #
            #    kind = entities[path]
            #    del entities[path]
            #
            #    elements = []
            #    if kind == "dir":
            #        for key in entities.keys():
            #            if key.startswith(path + "/"):
            #                elements.append(key)
            #    for elem in elements:
            #        del entities[elem]
            #else:
            #    assert path not in entities

            print "%9s %-7s %-4s %s%s" % \
                ("r%d:%d" % (dump.get_rev_nr(), txn),
                 action,
                 kind,
                 path,
                 " (copied from %s [r%d])" % \
                     (node.get_copy_from_path(), node.get_copy_from_rev())
                     if node.has_copy_from() else "")

            if kind == "file" and action in ["add", "change"] and \
               node.has_text() and node.has_md5():
                import hashlib
                md5 = hashlib.md5()
                text = node.text_open()
                length = node.get_text_length()
                data = node.text_read(text, length)
                assert len(data) == length
                node.text_close(text); del text
                md5.update(data); del data
                assert node.get_text_md5() == md5.hexdigest()
                del md5

            if kind == "file" and action in ["add", "change"]:
                text = node.text_open()
                length = node.get_text_length()
                data = node.text_read(text, length)
                git('hash-object', '-w', '--stdin',
                    '--path=%s' % path, '--no-filters', input = data)
                del text
                del data

            # node.write_text_to_file(handle)

            txn += 1

    dump.close()

if __name__ == "__main__":
    #if len(sys.argv) < 2:
    #    sys.stderr.write("usage: migrate.py <FILE>\n")
    #    sys.exit(1)
    #else:
    #    read_dumpfile(sys.argv[1])

    commit = GitCommit()
    commit.update('foo/bar/baz.c', GitBlob('baz.c', '#include <stdio.h>\n'))
    commit.author_name  = 'John Wiegley'
    commit.author_email = 'johnw@boostpro.com'
    commit.author_date  = '2005-04-07T22:13:13'
    commit.write("This is a sample commit.\n")

    branch = GitBranch()
    branch.update(commit)

    commit = commit.fork()      # makes a new commit similar to the old one
    commit.remove('foo/bar/baz.c')
    commit.author_name  = 'John Wiegley'
    commit.author_email = 'johnw@boostpro.com'
    commit.author_date  = '2005-04-10T22:13:13'
    commit.write("This removes the previous file.\n")

    branch.update(commit)

    git('symbolic-ref', 'HEAD', 'refs/heads/%s' % branch.name)
