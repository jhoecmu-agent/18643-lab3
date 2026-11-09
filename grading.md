# 18-643 Fall 2026 - Lab 3 Grading & Submission Review

**Course:** CMU 18-643: Reconfigurable Logic: Technology, Architecture and Applications  
**Handout:** #7 / Lab 3: Hardware Accelerated Computation  
**Target Platform:** Ultra96v2 Zynq UltraScale+ SoC (Vitis OpenCL Flow)  

---

## 1. Rubric Dimension 1: Submission Compliance (0 – 5 Points)

This dimension evaluates each team's adherence to the official submission specifications set forth in [H07-lab3-2026.pdf](file:///home/user/Desktop/18643-lab3/H07-lab3-2026.pdf#L4) (pages 2 and 4):
1. **Archive & Directory Naming:** `<team_name>_lab3.zip` containing `<team_name>_lab3/`, where `<team_name>` is an alphabetical concatenation of team members' AndrewIDs connected by `_`.
2. **Datapath Block Diagram:** Inclusion of a drawn block diagram of the synthesized kernel datapath (`.pdf`, `.png`, `.jpg`).
3. **Workspace Packaging:** Inclusion of a zip file of the complete workspace directory.
4. **Build Artifact Cleaning:** Proper purging of build artifacts (via the "broom" icon in Vitis) to maintain a compact, clean repository.
5. **Permitted Edits Scope:** Full flexibility to edit [`krnl_cnn.cpp`](file:///home/user/Desktop/18643-lab3/starter_src/18643_cnn_src/krnl_cnn.cpp) and [`kernel643.h`](file:///home/user/Desktop/18643-lab3/starter_src/18643_cnn_src/kernel643.h); strict prohibition against modifying other starter files (`instance643.h`, `cnn.cpp`, `cnn.h`, `util643.h`).

### 1.1 Scoring Standard (0 to 5 Scale)

| Score | Rating Tier | Criteria & Architectural Signature | Teams |
| :---: | :--- | :--- | :--- |
| **5 / 5** | **Exemplary Compliance** | Flawless adherence to all submission specifications: proper alphabetical AndrewID directory/zip naming, valid datapath block diagram, properly packaged workspace `.zip`, thoroughly cleaned build tree (< 1 MB archive), and compliant code modifications. | CRT |
| **4 / 5** | **Strong Compliance (Single Minor Packaging Blemish)** | All essential technical deliverables present and compliant (code, diagram, complete workspace), but with a single packaging deviation: either forgetting to purge build artifacts before creating the workspace zip, or omitting the nested zip wrapper while thoroughly cleaning build files, or minor AndrewID ordering permutation. | CJK, CJS, CKL, GPY, JJS |
| **3 / 5** | **Moderate Compliance (Dual Packaging Deviations)** | Core design code and diagram present, but compounded by two packaging deviations: (a) using group acronym name + uncleaned build tree, or (b) submitting raw unzipped workspace + uncleaned build tree (24 MB – 160 MB), or (c) acronym naming + raw workspace directory + minor diagnostic edits. | AJS, BJO, CHL, DEL, DGW, FLX, JMM, LSX, MPV, SZZ |
| **2 / 5** | **Substantial Deficiencies (Triple Packaging Failure)** | Submissions exhibiting three concurrent packaging failures: acronym naming + raw unzipped workspace folder + large uncleaned build tree (> 800 files). | DLW |
| **1 / 5** | **Severe Non-Compliance (Critical Missing Deliverables)** | Missing primary deliverables required for evaluation: absence of the datapath block diagram and omission of `krnl_cnn.cpp` from the submission archive, along with uncleaned build artifacts. | BSW |
| **0 / 5** | **Void / Empty Submission** | Submission archive contains 0 bytes of payload (empty folder); missing workspace, code, and diagram. | — |

---

## 2. Master Evaluation Summary Table

| # | Group | AndrewID Naming? | Datapath Diagram? | Workspace Archive? | Build Cleaned? | Permitted Edits Only? | Compliance Score (/5) | Overall Compliance Status |
| :-: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| 1 | **AJS** | ❌ Acronym (`ajs`) | ✅ `datapath.png` | ⚠️ Raw directory | ✅ **Cleaned (198 KB)** | ⚠️ Diagnostic edit in `cnn_host/cnn.cpp` | **3 / 5** | **PARTIAL** (Acronym name, Raw WS, edit in cnn.cpp) |
| 2 | **BJO** | ❌ Acronym (`BJO`) | ✅ `block_diagram.pdf` | ✅ Nested `BJO_lab3_workspace.zip` | ❌ Uncleaned (90 MB) | ✅ Compliant | **3 / 5** | **PARTIAL** (Acronym name, Uncleaned build) |
| 3 | **BSW** | ❌ Acronym (`BWS`) | ❌ None | ⚠️ Raw directory | ❌ Uncleaned (42 MB) | ❌ Missing `krnl_cnn.cpp` | **1 / 5** | **NON-COMPLIANT** (Missing kernel & diagram) |
| 4 | **CHL** | ❌ Acronym (`CHL`) | ✅ `block_diagram.pdf` | ✅ Nested `lab3.zip` | ❌ Uncleaned (35 MB) | ✅ Compliant | **3 / 5** | **PARTIAL** (Acronym name, Uncleaned build) |
| 5 | **CJK** | ⚠️ Semi (`akim2` 3rd) | ✅ `kernel_block_diagram.pdf` | ✅ Nested `lab3_workspace.zip` | ❌ Uncleaned (68 MB) | ✅ Compliant | **4 / 5** | **STRONG** (Uncleaned build) |
| 6 | **CJS** | ✅ Alphabetical | ✅ `blcok_diagram.png` | ✅ Nested `lab3_workspace.zip` | ❌ Uncleaned (37 MB) | ✅ Compliant | **4 / 5** | **STRONG** (Uncleaned build) |
| 7 | **CKL** | ✅ Alphabetical | ✅ `kernel_diagram.pdf` | ✅ Nested `lab3.zip` | ❌ Uncleaned (37 MB) | ✅ Compliant | **4 / 5** | **STRONG** (Uncleaned build) |
| 8 | **CRT** | ✅ Alphabetical | ✅ `datapath_block_diagram.pdf` | ✅ Nested `lab3_workspace.zip` | ✅ **Cleaned (387 KB)** | ✅ Compliant | **5 / 5** | **EXEMPLARY** (Flawless packaging) |
| 9 | **DEL** | ✅ Alphabetical | ✅ `lab3 diagram.pdf` | ⚠️ Raw directory | ❌ Uncleaned (24 MB) | ✅ Compliant | **3 / 5** | **MODERATE** (Raw WS, Uncleaned build) |
| 10 | **DGW** | ✅ Alphabetical | ✅ `block_diagram.png` | ⚠️ Raw directory | ❌ Uncleaned (73 MB) | ✅ Compliant | **3 / 5** | **MODERATE** (Raw WS, Uncleaned build) |
| 11 | **DLW** | ❌ Acronym (`dlw`) | ✅ `block_diagram.png` | ⚠️ Raw directory | ❌ Uncleaned (32 MB) | ✅ Compliant | **2 / 5** | **SUBSTANTIAL** (Acronym, Raw WS, Uncleaned) |
| 12 | **FLX** | ✅ Alphabetical | ✅ `kernel_datapath.jpg` | ⚠️ Raw directory | ❌ Uncleaned (48 MB) | ✅ Compliant | **3 / 5** | **MODERATE** (Raw WS, Uncleaned build) |
| 13 | **GPY** | ✅ Alphabetical | ✅ `kernel_datapath.pdf` | ✅ Nested `lab3_workspace.zip` | ❌ Uncleaned (37 MB) | ✅ Compliant | **4 / 5** | **STRONG** (Uncleaned build) |
| 14 | **JJS** | ✅ Alphabetical | ✅ `Datapath Block Diagram.png` | ⚠️ Raw directory | ✅ **Cleaned (113 KB)** | ✅ Compliant | **4 / 5** | **STRONG** (Cleaned build, Raw WS folder) |
| 15 | **JMM** | ✅ Alphabetical | ✅ `diagram.jpg` | ⚠️ Raw directory | ❌ Uncleaned (152 MB)| ✅ Compliant | **3 / 5** | **MODERATE** (Raw WS, Uncleaned build) |
| 16 | **LSX** | ✅ Alphabetical | ✅ `lab3_diagram.pdf` | ⚠️ Raw directory | ❌ Uncleaned (85 MB) | ✅ Compliant | **3 / 5** | **MODERATE** (Raw WS, Uncleaned build) |
| 17 | **MPV** | ❌ Acronym (`mpv`) | ✅ `mpv_lab3_block_diagram.png`| ✅ Nested workspace zip | ❌ Uncleaned (88 MB) | ✅ Compliant | **3 / 5** | **MODERATE** (Acronym name, Uncleaned build) |
| 18 | **SZZ** | ✅ Alphabetical | ✅ `Kernel Datapath.pdf` | ⚠️ Raw directory | ❌ Uncleaned (160 MB)| ✅ Compliant | **3 / 5** | **MODERATE** (Raw WS, Uncleaned build) |

---

## 3. Detailed Group Evaluations

### 1. AJS
- **Group Acronym:** AJS
- **Team Name / Directory:** `ajs_lab3`
- **Submission Archive:** `ajs_LATE_97622_15160954_ajs_lab3.zip` (198 KB updated archive)
- **Identified Members:** Misho Alexandrov, Mizuki Jiang, Kaloyan Stefanov (`kastefan`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (-1 pt):* Non-compliant. Used group acronym `ajs` rather than alphabetical AndrewIDs.
  - *Block Diagram (+1 pt):* Compliant. Included `datapath.png` (148.8 KB).
  - *Workspace Archive (-0.5 pt):* Minor packaging deviation. Submitted unzipped workspace folder `lab3/` directly inside `ajs_lab3/` rather than as a nested workspace `.zip`.
  - *Build Cleaning (+1 pt):* Exemplary. Cleared all build artifacts (0 build files, total archive size only 198 KB).
  - *Permitted Edits (-0.5 pt):* Modified `kernel643.h` ($T_R=13, T_C=13, T_M=8, T_N=192$) and `krnl_cnn.cpp` as permitted. Starter files `instance643.h`, `cnn.h`, and `util643.h` are pristine. However, `cnn_host/cnn.cpp` contains minor diagnostic modifications in `verify()` (prints mismatch details across the first 15 failing elements before returning `bad == 0`).

---

### 2. BJO
- **Group Acronym:** BJO
- **Team Name / Directory:** `BJO_lab3`
- **Submission Archive:** `bjo_LATE_92116_15119908_BJO_lab3.zip` (89.9 MB)
- **Identified Members:** T'saurus Beasley, Wellington Jiang, Nicholas Ortiz
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (-1 pt):* Used group acronym `BJO` instead of alphabetical AndrewIDs.
  - *Block Diagram (+1 pt):* Compliant. Included `block_diagram.pdf`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `BJO_lab3_workspace.zip`.
  - *Build Cleaning (-1 pt):* Non-compliant. Left 515 build and synthesis files (89.9 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 3. BSW (BWS)
- **Group Acronym:** BSW (BWS)
- **Team Name / Directory:** `BWS_lab3`
- **Submission Archive:** `bsw_LATE_144616_15163943_BWS_lab3.zip` (42.3 MB)
- **Identified Members:** Abnash Bassi, Upasana Sridhar, Chenxi Wan
- **Rubric Dimension 1 (Submission Compliance):** **1 / 5**
  - *Naming (-0.5 pt):* Used group acronym `BWS` instead of alphabetical AndrewIDs.
  - *Block Diagram (-1.5 pts):* **Missing.** No datapath diagram included in submission.
  - *Workspace Archive (-0.5 pt):* Submitted raw unzipped workspace directory instead of nested zip.
  - *Build Cleaning (-0.5 pt):* Left 587 build files uncleaned (42.3 MB archive).
  - *Code Integrity (-1.5 pts):* **Critical Defect.** Omitted `krnl_cnn.cpp` from the submission archive (workspace configuration pointed to an unsubmitted external AFS path).

---

### 4. CHL
- **Group Acronym:** CHL
- **Team Name / Directory:** `CHL_lab3`
- **Submission Archive:** `chl_LATE_151187_15160251_CHL_lab3-1.zip` (35.1 MB)
- **Identified Members:** Ryder Chou, Mu-Wei Hsieh, Yosen Lin
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (-1 pt):* Used group acronym `CHL` instead of alphabetical AndrewIDs.
  - *Block Diagram (+1 pt):* Compliant. Included `CHL_lab3/block_diagram.pdf`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `CHL_lab3/lab3.zip`.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 369 uncleaned build files (35.1 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 5. CJK
- **Group Acronym:** CJK
- **Team Name / Directory:** `rickycui_yantingj_akim2_lab3`
- **Submission Archive:** `cjk_LATE_142202_15162393_rickycui_yantingj_akim2_lab3.zip` (67.8 MB)
- **Identified Members:** Ricky Cui (`rickycui`), Yanting Ji (`yantingj`), Andrew Kim (`akim2`)
- **Rubric Dimension 1 (Submission Compliance):** **4 / 5**
  - *Naming (-0.5 pt):* Used AndrewIDs, but `akim2` was placed third instead of strictly alphabetical order.
  - *Block Diagram (+1 pt):* Compliant. Included `kernel_block_diagram.pdf`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `lab3_workspace.zip`.
  - *Build Cleaning (-0.5 pt):* Failed to purge build tree before zipping (569 build files, 67.8 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 6. CJS
- **Group Acronym:** CJS
- **Team Name / Directory:** `chiehhss_shukangj_yichingc_lab3`
- **Submission Archive:** `cjs_141367_15113610_chiehhss_shukangj_yichingc_lab3.zip` (36.8 MB)
- **Identified Members:** Chieh-Hsin Shih (`chiehhss`), Shukang Jia (`shukangj`), Yi-Ching Chen (`yichingc`)
- **Rubric Dimension 1 (Submission Compliance):** **4 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `blcok_diagram.png`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `lab3_workspace.zip`.
  - *Build Cleaning (-1 pt):* Non-compliant. Failed to purge build artifacts (513 build files, 36.8 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 7. CKL
- **Group Acronym:** CKL
- **Team Name / Directory:** `akrzyzan_dchan2_kodyl_lab3`
- **Submission Archive:** `ckl_97773_15116707_akrzyzan_dchan2_kodyl_lab3.zip` (36.9 MB)
- **Identified Members:** Ania Krzyzanska (`akrzyzan`), David Chan (`dchan2`), Kody Liang (`kodyl`)
- **Rubric Dimension 1 (Submission Compliance):** **4 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `kernel_diagram.pdf`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `lab3.zip`.
  - *Build Cleaning (-1 pt):* Non-compliant. Failed to purge build artifacts (525 build files, 36.9 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Main workspace files pristine (auxiliary `cnn_test/` was used for C debugging).

---

### 8. CRT
- **Group Acronym:** CRT
- **Team Name / Directory:** `cjyu_jcestero_jrogan_otank_lab3`
- **Submission Archive:** `crt_98394_15116513_cjyu_jcestero_jrogan_otank_lab3-1.zip` (387 KB)
- **Identified Members:** Christian Yu (`cjyu`), Jose Cestero (`jcestero`), John Rogan (`jrogan`), Om Tank (`otank`)
- **Rubric Dimension 1 (Submission Compliance):** **5 / 5 (Exemplary)**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included both `datapath_block_diagram.pdf` and `.png`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `lab3_workspace.zip`.
  - *Build Cleaning (+1 pt):* Exemplary. Properly clicked the "broom" icon; cleared all build artifacts (0 build files, total archive size 387 KB).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 9. DEL
- **Group Acronym:** DEL
- **Team Name / Directory:** `fpel_ldas_sienahol_lab3`
- **Submission Archive:** `del_92659_15117835_fpel_ldas_sienahol_lab3-1.zip` (24.1 MB)
- **Identified Members:** Feya Epel (`fpel`), Lilly Das (`ldas`), Siena Lee (`sienahol`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `lab3 diagram.pdf`.
  - *Workspace Archive (-1 pt):* Non-compliant. Subdirectory contains unzipped workspace rather than a nested zip.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 461 uncleaned build files (24.1 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 10. DGW
- **Group Acronym:** DGW
- **Team Name / Directory:** `mgong2_sdupont_tongwu2_lab3`
- **Submission Archive:** `dgw_98738_15077348_mgong2_sdupont_tongwu2_lab3.zip` (72.7 MB)
- **Identified Members:** Ming Gong (`mgong2`), Soren Dupont (`sdupont`), Tong Wu (`tongwu2`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `block_diagram.png` (plus supplementary `IMG_2201.jpg`).
  - *Workspace Archive (-1 pt):* Non-compliant. Submitted unzipped workspace directory directly.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 288 uncleaned build files (72.7 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 11. DLW
- **Group Acronym:** DLW
- **Team Name / Directory:** `dlw_lab3`
- **Submission Archive:** `dlw_LATE_75009_15160450_dlw_lab3.zip` (31.7 MB)
- **Identified Members:** Caleb Doan (`cdoan`), Diac Liu (`diacl`), Yuyang Wu (`yuyangw2`)
- **Rubric Dimension 1 (Submission Compliance):** **2 / 5**
  - *Naming (-1 pt):* Used group acronym `dlw` instead of alphabetical AndrewIDs.
  - *Block Diagram (+1 pt):* Compliant. Included `lab3/block_diagram.png`.
  - *Workspace Archive (-1 pt):* Submitted unzipped workspace directory directly inside zip.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 814 uncleaned build files (31.7 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 12. FLX
- **Group Acronym:** FLX
- **Team Name / Directory:** `freddyfu_peiranx_wenzhuol_lab3`
- **Submission Archive:** `flx_135476_15115377_freddyfu_peiranx_wenzhuol_lab3.zip` (47.9 MB)
- **Identified Members:** Freddy Fu (`freddyfu`), Raymond Xiang (`peiranx`), Wenzhuo Li (`wenzhuol`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `kernel_datapath.jpg`.
  - *Workspace Archive (-1 pt):* Non-compliant. Submitted unzipped workspace directory directly.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 421 uncleaned build files (47.9 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 13. GPY
- **Group Acronym:** GPY
- **Team Name / Directory:** `alpan_shengfeg_sutongy_lab3`
- **Submission Archive:** `gpy_151161_15119231_alpan_shengfeg_sutongy_lab3.zip` (36.8 MB)
- **Identified Members:** Andrew Pan (`alpan`), Shengfei Gu (`shengfeg`), Sutong Yao (`sutongy`)
- **Rubric Dimension 1 (Submission Compliance):** **4 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `kernel_datapath.pdf`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `lab3_workspace.zip`.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 692 uncleaned build files (36.8 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 14. JJS
- **Group Acronym:** JJS
- **Team Name / Directory:** `garysu_rachelji_yanxinj_lab3`
- **Submission Archive:** `jjs_143329_15117582_garysu_rachelji_yanxinj_lab3.zip` (113 KB)
- **Identified Members:** Gary Su (`garysu`), Rachel Jiang (`rachelji`), Yanxin Jiang (`yanxinj`)
- **Rubric Dimension 1 (Submission Compliance):** **4 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `Datapath Block Diagram.png`.
  - *Workspace Archive (-1 pt):* Minor packaging oversight. Included unzipped workspace folder `lab3/` directly rather than as a nested `.zip`.
  - *Build Cleaning (+1 pt):* Exemplary. Cleared all build artifacts (0 build files, total archive size 113 KB).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 15. JMM
- **Group Acronym:** JMM
- **Team Name / Directory:** `jjagielo_kminihan_zrmoore_lab3`
- **Submission Archive:** `jmm_LATE_142309_15158865_jjagielo_kminihan_zrmoore_lab3-1.zip` (152.1 MB)
- **Identified Members:** Jason Jagielo (`jjagielo`), Kyle Minihan (`kminihan`), Zach Moore (`zrmoore`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation (`jjagielo_kminihan_zrmoore`).
  - *Block Diagram (+1 pt):* Compliant. Included `lab3/diagram.jpg`.
  - *Workspace Archive (-1 pt):* Non-compliant. Submitted unzipped workspace directory directly inside archive.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 1,079 uncleaned build files (152.1 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 16. LSX
- **Group Acronym:** LSX
- **Team Name / Directory:** `aqx_jieunlim_serenas_lab3`
- **Submission Archive:** `lsx_LATE_95470_15163599_aqx_jieunlim_serenas_lab3.zip` (84.6 MB)
- **Identified Members:** Aaron Xu (`aqx`), Jieun Lim (`jieunlim`), Serena Shih (`serenas`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `lab3_diagram.pdf`.
  - *Workspace Archive (-1 pt):* Non-compliant. Subdirectory contains unzipped workspace instead of a nested zip.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 515 uncleaned build files (84.6 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 17. MPV
- **Group Acronym:** MPV
- **Team Name / Directory:** `mpv_lab3`
- **Submission Archive:** `mpv_142631_15119319_mpv_lab3-1.zip` (88.1 MB)
- **Identified Members:** Kaeshav Mandyam (`kmandyam`), Krish Patel (`kpatel`), Suchir Vemulapalli (`svemulap`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (-1 pt):* Used group acronym `mpv` instead of alphabetical AndrewIDs.
  - *Block Diagram (+1 pt):* Compliant. Included `mpv_lab3_block_diagram.png`.
  - *Workspace Archive (+1 pt):* Compliant packaging. Provided nested `mpv_lab3_code_workspace.zip`.
  - *Build Cleaning (-1 pt):* Non-compliant. Included 582 uncleaned build files (88.1 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files pristine.

---

### 18. SZZ
- **Group Acronym:** SZZ
- **Team Name / Directory:** `chenhao7_rzamir_sseshan_lab3`
- **Submission Archive:** `szz_LATE_71254_15160116_chenhao7_rzamir_sseshan_lab3.zip` (159.9 MB)
- **Identified Members:** Chenhao (Rock) Zhang (`chenhao7`), Rayyan Zamir (`rzamir`), Sanjay Seshan (`sseshan`)
- **Rubric Dimension 1 (Submission Compliance):** **3 / 5**
  - *Naming (+1 pt):* Compliant. Exact alphabetical AndrewIDs concatenation.
  - *Block Diagram (+1 pt):* Compliant. Included `Kernel Datapath.pdf`.
  - *Workspace Archive (-1 pt):* Non-compliant. Submitted unzipped workspace directory directly.
  - *Build Cleaning (-1 pt):* Non-compliant. Heaviest uncleaned build tree in class (935 build files, 159.9 MB archive).
  - *Permitted Edits (+1 pt):* Compliant. Modified `kernel643.h` and `krnl_cnn.cpp`; starter files have header comments stripped but functional code is intact.
